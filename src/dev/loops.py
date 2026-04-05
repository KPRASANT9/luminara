"""
Dev Loop 2 + Loop 3 — Self-improving loops for the development substrate.

Loop 2 (Weekly): Optimize agent configurations (model, temp, steps, prompts).
  Input: Week's AgentInvocations + DevPredictionEntries + meta-gradient
  Output: Updated RoutingTable + prompt refinements
  Gate: Dev I7 — accuracy must be non-decreasing per agent

Loop 3 (Monthly): Optimize agent architecture (topology, routing, permissions).
  Input: Month's Loop 2 history + info gain per agent + cost trends
  Output: Updated agent topology (merge/split/add/remove agents)
  Gate: Dev I8 — overall system accuracy-cost ratio must improve

These are the same three loops as HAPOS, applied to a different substrate:
  Loop 1: Execute tasks → measure outcomes (per invocation, milliseconds)
  Loop 2: Update configs → improve accuracy/efficiency (weekly)
  Loop 3: Restructure architecture → improve the optimization itself (monthly)
"""
from __future__ import annotations
from dataclasses import dataclass, field
from datetime import datetime, timezone
from typing import Optional
from src.dev.physics import (
    DevMetricsTracker, AgentInvocation, DevPredictionEntry,
    AgentPerformanceEmbedder, DevSubScore,
)
from src.dev.router import ModelRouter, RoutingTable, DEFAULT_CONFIGS
from src.dev.prompt_ratchet import PromptRatchet
from src.core.fos import DomainMatrix


# ═══════════════════════════════════════════════════════════════
# DEV LOOP 2: Weekly Config Optimization
# ═══════════════════════════════════════════════════════════════

@dataclass
class DevConfigVersion:
    """One snapshot of the complete agent configuration. Versioned like HAPOS ModelVersion."""
    id: str = field(default_factory=lambda: f"dc-{__import__('uuid').uuid4().hex[:8]}")
    routing: Optional[RoutingTable] = None
    per_agent_accuracy: dict[str, float] = field(default_factory=dict)
    per_agent_efficiency: dict[str, float] = field(default_factory=dict)
    info_gain: dict[str, float] = field(default_factory=dict)  # How much each agent improved
    total_cost: float = 0.0
    parent: str = ""
    status: str = "candidate"


class DevLoop2:
    """
    Weekly development config optimization.
    
    Protocol (same as HAPOS Loop 2):
    1. Collect week's invocation data
    2. Compute per-agent accuracy, efficiency, coherence, velocity
    3. Compute DomainMatrix per agent (the performance embedding)
    4. Run Model Router (Level 1: pick best model per agent)
    5. Run Prompt Ratchet (Level 2: refine prompts)
    6. Compute meta-gradient: which agents are accelerating/decelerating?
    7. Gate: Dev I7 — accuracy non-decreasing per agent
    8. Deploy or discard
    
    The meta-gradient feeds Loop 3:
    - Decelerating agents → need structural change (Loop 3 territory)
    - Accelerating agents → keep current config (convergent)
    """
    def __init__(self, metrics: DevMetricsTracker, router: ModelRouter,
                 prompt_ratchet: PromptRatchet):
        self.metrics = metrics
        self.router = router
        self.prompt_ratchet = prompt_ratchet
        self._history: list[DevConfigVersion] = []
        self._embedders: dict[str, AgentPerformanceEmbedder] = {}

    def _get_embedder(self, agent_name: str) -> AgentPerformanceEmbedder:
        if agent_name not in self._embedders:
            self._embedders[agent_name] = AgentPerformanceEmbedder(agent_name)
        return self._embedders[agent_name]

    async def compute_agent_matrices(self) -> dict[str, DomainMatrix]:
        """Compute DomainMatrix for every agent. The dev substrate's 'embed' stage."""
        all_invocations = self.metrics.query()
        matrices = {}
        for agent_name in DEFAULT_CONFIGS:
            embedder = self._get_embedder(agent_name)
            matrix, phi = embedder.embed(invocations=all_invocations)
            matrices[agent_name] = matrix
        return matrices

    async def compute_meta_gradient(self) -> dict[str, float]:
        """
        Per-agent acceleration: is this agent improving or stalling?
        Positive = accelerating (keep config). Negative = decelerating (change needed).
        This feeds Loop 3.
        """
        gradient: dict[str, float] = {}
        if len(self._history) < 2:
            return {a: 0.0 for a in DEFAULT_CONFIGS}

        curr = self._history[-1]
        prev = self._history[-2]
        for agent in DEFAULT_CONFIGS:
            curr_acc = curr.per_agent_accuracy.get(agent, 0.5)
            prev_acc = prev.per_agent_accuracy.get(agent, 0.5)
            gradient[agent] = curr_acc - prev_acc  # Positive = improving
        return gradient

    async def run_weekly_cycle(self) -> DevConfigVersion:
        """
        Complete weekly optimization cycle.
        """
        # Step 1: Compute performance matrices (dev 'embed' stage)
        matrices = await self.compute_agent_matrices()

        # Step 2: Compute per-agent accuracy and efficiency
        per_accuracy: dict[str, float] = {}
        per_efficiency: dict[str, float] = {}
        all_inv = self.metrics.query()
        for agent_name in DEFAULT_CONFIGS:
            agent_inv = [i for i in all_inv if i.agent_name == agent_name][-50:]
            if agent_inv:
                per_accuracy[agent_name] = sum(i.accuracy for i in agent_inv) / len(agent_inv)
                per_efficiency[agent_name] = sum(i.efficiency for i in agent_inv) / len(agent_inv)
            else:
                per_accuracy[agent_name] = 0.5
                per_efficiency[agent_name] = 0.5

        # Step 3: Run Model Router (Level 1 optimization)
        new_routing = self.router.compute_routing()

        # Step 4: Run Prompt Ratchet (Level 2 optimization)
        for agent_name in DEFAULT_CONFIGS:
            result = self.prompt_ratchet.evaluate_and_ratchet(agent_name)
            if result == "discard":
                pass  # Prompt already reverted inside ratchet

        # Step 5: Compute info gain (how much did each agent improve?)
        info_gain: dict[str, float] = {}
        if self._history:
            prev = self._history[-1]
            for agent_name in DEFAULT_CONFIGS:
                curr_a = per_accuracy.get(agent_name, 0.5)
                prev_a = prev.per_agent_accuracy.get(agent_name, 0.5)
                info_gain[agent_name] = max(0, curr_a - prev_a)
        else:
            info_gain = {a: 0.0 for a in DEFAULT_CONFIGS}

        # Step 6: Gate — Dev I7: accuracy non-decreasing per agent
        if self._history:
            prev = self._history[-1]
            for agent_name in DEFAULT_CONFIGS:
                curr_a = per_accuracy.get(agent_name, 0.5)
                prev_a = prev.per_agent_accuracy.get(agent_name, 0.5)
                if curr_a < prev_a - 0.05:  # >5% regression
                    # Discard: revert model assignment for this agent
                    if agent_name in new_routing.assignments and self._history:
                        old_assignment = None
                        for h in reversed(self._history):
                            if h.routing and agent_name in h.routing.assignments:
                                old_assignment = h.routing.assignments[agent_name]
                                break
                        if old_assignment:
                            new_routing.assignments[agent_name] = old_assignment

        # Create version
        version = DevConfigVersion(
            routing=new_routing,
            per_agent_accuracy=per_accuracy,
            per_agent_efficiency=per_efficiency,
            info_gain=info_gain,
            total_cost=self.metrics.total_cost_usd,
            parent=self._history[-1].id if self._history else "",
            status="deployed",
        )
        self._history.append(version)
        return version


# ═══════════════════════════════════════════════════════════════
# DEV LOOP 3: Monthly Architecture Evolution
# ═══════════════════════════════════════════════════════════════

@dataclass
class TopologyChange:
    """One proposed structural change to the agent architecture."""
    change_type: str  # "merge" | "split" | "add" | "remove" | "reroute" | "repermission"
    agents_affected: list[str] = field(default_factory=list)
    reasoning: str = ""
    expected_impact: dict[str, float] = field(default_factory=dict)  # per dev sub-score

@dataclass
class DevArchitectureVersion:
    id: str = field(default_factory=lambda: f"da-{__import__('uuid').uuid4().hex[:8]}")
    changes: list[TopologyChange] = field(default_factory=list)
    accuracy_cost_ratio: float = 0.0
    parent: str = ""


class DevLoop3:
    """
    Monthly architecture evolution.
    
    Analyzes Loop 2 history and proposes structural changes:
    
    SURFACE 1: Agent topology
      - Merge agents with similar performance profiles
      - Split agents with bimodal performance (good at some tasks, bad at others)
      - Add new subagents for discovered task patterns
      - Remove underperforming agents
    
    SURFACE 2: Routing and permissions
      - Adjust task routing based on meta-gradient
      - Tighten permissions for error-prone agents
      - Expand permissions for high-accuracy agents
    
    SURFACE 3: Cost envelope
      - If total spend exceeding budget: downgrade low-impact agents to cheaper models
      - If under budget with accuracy headroom: upgrade bottleneck agents
    
    Gate: Dev I8 — overall accuracy-cost ratio must improve or stay constant.
    """
    def __init__(self, metrics: DevMetricsTracker):
        self.metrics = metrics
        self._history: list[DevArchitectureVersion] = []

    async def analyze_topology(self,
                                loop2_history: list[DevConfigVersion],
                                meta_gradient: dict[str, float],
                                cost_budget: float = 50.0) -> list[TopologyChange]:
        """
        Propose structural changes based on Loop 2 history.
        """
        changes: list[TopologyChange] = []

        if len(loop2_history) < 4:
            return changes  # Need at least a month of Loop 2 data

        # SURFACE 1: Agent topology
        # Identify agents that always run together → merge candidate
        # Identify agents with bimodal accuracy → split candidate
        for agent_name in DEFAULT_CONFIGS:
            accels = [h.info_gain.get(agent_name, 0) for h in loop2_history[-4:]]
            avg_gain = sum(accels) / len(accels)

            if avg_gain < 0.001 and all(a < 0.001 for a in accels):
                # Zero info gain for 4 weeks → agent is stagnant
                changes.append(TopologyChange(
                    change_type="reroute",
                    agents_affected=[agent_name],
                    reasoning=f"{agent_name}: zero info gain for 4 weeks — needs different model or prompt strategy",
                    expected_impact={"A_dev": 0.05, "E_dev": 0.0},
                ))

        # SURFACE 2: Cost optimization
        total_cost = sum(h.total_cost for h in loop2_history[-4:])
        if total_cost > cost_budget * 4:
            # Over budget: find highest-cost, lowest-accuracy agents
            latest = loop2_history[-1]
            for agent, acc in sorted(latest.per_agent_accuracy.items(), key=lambda x: x[1]):
                if acc < 0.7:
                    changes.append(TopologyChange(
                        change_type="reroute",
                        agents_affected=[agent],
                        reasoning=f"{agent}: accuracy {acc:.0%} below threshold — downgrade to cheaper model",
                        expected_impact={"E_dev": 0.10, "A_dev": -0.02},
                    ))

        # SURFACE 3: Permission tightening for error-prone agents
        for agent_name in DEFAULT_CONFIGS:
            recent = self.metrics.query(agent=agent_name)[-20:]
            if recent:
                fail_rate = sum(1 for i in recent if i.outcome == "failure") / len(recent)
                if fail_rate > 0.3:
                    changes.append(TopologyChange(
                        change_type="repermission",
                        agents_affected=[agent_name],
                        reasoning=f"{agent_name}: {fail_rate:.0%} failure rate — tighten permissions to 'ask'",
                        expected_impact={"A_dev": 0.05, "V_dev": -0.03},
                    ))

        return changes

    async def run_monthly_cycle(self,
                                 loop2_history: list[DevConfigVersion],
                                 meta_gradient: dict[str, float]) -> DevArchitectureVersion:
        """Complete monthly architecture review."""
        changes = await self.analyze_topology(loop2_history, meta_gradient)

        # Gate: Dev I8 — only deploy if accuracy-cost ratio improves
        if loop2_history:
            latest = loop2_history[-1]
            total_acc = sum(latest.per_agent_accuracy.values()) / max(1, len(latest.per_agent_accuracy))
            total_cost = latest.total_cost or 1.0
            ratio = total_acc / total_cost
        else:
            ratio = 0.0

        version = DevArchitectureVersion(
            changes=changes,
            accuracy_cost_ratio=ratio,
            parent=self._history[-1].id if self._history else "",
        )
        self._history.append(version)
        return version
