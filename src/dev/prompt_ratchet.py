"""
Prompt Ratchet — Embedding Ratchet for agent prompts.

In HAPOS: W_d projects physics state into sub-score components.
In DevHAPOS: agent prompts project the task context into agent behavior.

The prompt IS the projection matrix of the development substrate.
Different prompts make the same model produce different outputs from the same input,
exactly as different W_d makes the same M_d produce different Φ components.

The Prompt Ratchet:
  1. Records which prompt version produced which outcome
  2. Identifies prompt sections that correlate with failures
  3. Proposes prompt modifications (add context, clarify constraints, add examples)
  4. Tests the modified prompt on the next task
  5. Keeps improvements, discards regressions
  6. Logs every experiment

Timescale: Daily (after each task batch).
Level: 2 (embedding projection — between param tuning and composition).
Metric: invariant pass rate PER PROMPT VERSION.
"""
from __future__ import annotations
import uuid
from dataclasses import dataclass, field
from datetime import datetime, timezone
from typing import Optional
from src.core.fos import RatchetExperiment


@dataclass
class PromptVersion:
    """Immutable prompt version. Never overwritten — always versioned."""
    id: str = field(default_factory=lambda: f"p-{uuid.uuid4().hex[:8]}")
    agent_name: str = ""
    content: str = ""                    # Full prompt text
    sections: dict[str, str] = field(default_factory=dict)  # Named sections for granular tracking
    created_at: datetime = field(default_factory=lambda: datetime.now(timezone.utc))
    parent_id: str = ""                  # Which version this was derived from

    # Performance (accumulated from invocations using this version)
    invocation_count: int = 0
    success_count: int = 0
    total_tokens: int = 0

    @property
    def pass_rate(self) -> float:
        return self.success_count / max(1, self.invocation_count)

    @property
    def tokens_per_success(self) -> float:
        return self.total_tokens / max(1, self.success_count)


@dataclass
class PromptModification:
    """One proposed change to a prompt section."""
    section: str            # Which section to modify
    modification_type: str  # "add_context" | "add_example" | "clarify" | "remove_ambiguity" | "restructure"
    original: str
    proposed: str
    reasoning: str


class PromptRatchet:
    """
    Optimizes agent prompts via the ratchet protocol.
    
    This is Level 2 optimization for the dev substrate:
      Level 1 (Model Router): chooses model/temp/steps — changes compute
      Level 2 (THIS): refines prompts — changes what the model perceives
      Level 3 (Architecture Evolver): restructures agent topology — changes composition
    
    The ratchet analyzes prompt-outcome correlations:
      - Which prompt sections are present in successful vs failed invocations?
      - Which sections produce the most variance in outcomes?
      - Where does the prompt lack specificity that correlates with failures?
    
    Then proposes modifications and tests them:
      - If modified prompt → higher pass rate: KEEP (commit new version)
      - If modified prompt → same or lower: DISCARD (revert to previous)
      - Log every experiment regardless
    """
    def __init__(self):
        self._versions: dict[str, list[PromptVersion]] = {}  # agent → version history
        self._experiments: list[RatchetExperiment] = []

    def register_version(self, version: PromptVersion) -> None:
        """Register a prompt version for an agent."""
        self._versions.setdefault(version.agent_name, []).append(version)

    def record_outcome(self, agent_name: str, prompt_version_id: str,
                       success: bool, tokens: int) -> None:
        """Record an invocation outcome against a prompt version."""
        versions = self._versions.get(agent_name, [])
        for v in versions:
            if v.id == prompt_version_id:
                v.invocation_count += 1
                v.total_tokens += tokens
                if success:
                    v.success_count += 1
                break

    def current_version(self, agent_name: str) -> Optional[PromptVersion]:
        """Get the latest prompt version for an agent."""
        versions = self._versions.get(agent_name, [])
        return versions[-1] if versions else None

    def analyze_failure_patterns(self, agent_name: str) -> list[str]:
        """
        Identify which prompt sections correlate with failures.
        Returns section names that need modification.
        """
        versions = self._versions.get(agent_name, [])
        if len(versions) < 2:
            return []

        # Compare sections between high-performing and low-performing versions
        weak_sections = []
        current = versions[-1]
        if current.pass_rate < 0.8:  # Below 80% pass rate
            for section_name in current.sections:
                # Find if any previous version had this section and performed better
                for prev in versions[:-1]:
                    if (section_name in prev.sections and
                            prev.pass_rate > current.pass_rate + 0.1):
                        weak_sections.append(section_name)
                        break

        return weak_sections

    def propose_modifications(self, agent_name: str) -> list[PromptModification]:
        """
        Generate modification proposals for weak prompt sections.
        Based on failure pattern analysis.
        """
        weak = self.analyze_failure_patterns(agent_name)
        current = self.current_version(agent_name)
        if not current:
            return []

        mods: list[PromptModification] = []
        for section in weak:
            original = current.sections.get(section, "")
            mods.append(PromptModification(
                section=section,
                modification_type="clarify",
                original=original,
                proposed=original,  # In production: LLM generates improved version
                reasoning=f"Section '{section}' correlates with {current.pass_rate:.0%} pass rate",
            ))
        return mods

    def apply_modification(self, agent_name: str,
                           modification: PromptModification) -> PromptVersion:
        """
        Create a new prompt version with the modification applied.
        The ratchet: this version will be tested. If it improves pass rate, it stays.
        """
        current = self.current_version(agent_name)
        new_sections = dict(current.sections) if current else {}
        new_sections[modification.section] = modification.proposed

        new_content = "\n\n".join(
            f"## {k}\n{v}" for k, v in new_sections.items()
        )

        new_version = PromptVersion(
            agent_name=agent_name,
            content=new_content,
            sections=new_sections,
            parent_id=current.id if current else "",
        )
        self.register_version(new_version)

        # Log the experiment
        exp = RatchetExperiment(
            level=2, domain=f"prompt_{agent_name}",
            baseline=current.pass_rate if current else 0.5,
            description=f"Prompt modification: {modification.modification_type} on {modification.section}",
        )
        self._experiments.append(exp)

        return new_version

    def evaluate_and_ratchet(self, agent_name: str,
                              min_invocations: int = 5) -> Optional[str]:
        """
        Evaluate current prompt version vs previous.
        If current is worse AND has enough data: revert (discard).
        If current is better: keep (commit).
        
        Returns: "keep", "discard", or None (insufficient data).
        """
        versions = self._versions.get(agent_name, [])
        if len(versions) < 2:
            return None

        current = versions[-1]
        previous = versions[-2]

        if current.invocation_count < min_invocations:
            return None  # Not enough data yet

        if current.pass_rate > previous.pass_rate + 0.01:
            # Improvement: KEEP
            return "keep"
        elif current.pass_rate < previous.pass_rate - 0.05:
            # Regression: DISCARD — revert to previous
            self._versions[agent_name].pop()  # Remove current
            return "discard"
        else:
            # Neutral: keep (prefer newer, all else equal)
            return "keep"

    @property
    def experiment_count(self) -> int:
        return len(self._experiments)
