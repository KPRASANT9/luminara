"""
Model Router — assigns optimal model per agent per task type.

This is Loop 1 for the dev substrate: observe performance, route accordingly.
The router doesn't GUESS which model is best. It MEASURES via the AgentPerformanceMatrix
and picks the model that maximizes the accuracy-cost frontier for each agent×task pair.

The routing decision is:
  model* = argmax_m [ w_acc · accuracy(agent, task, m) 
                     + w_eff · efficiency(agent, task, m)
                     - w_cost · cost(agent, task, m) ]

This IS the dev substrate's equivalent of HAPOS Loop 1:
  sense (performance data) → predict (which model will work best) → act (assign model)
  → feedback (did invariants pass? what was cost?)

The router produces an updated opencode.json agent config each cycle.
"""
from __future__ import annotations
from dataclasses import dataclass, field
from typing import Optional
from src.dev.physics import (
    AgentInvocation, DevMetricsTracker, MODEL_PRICING,
    AgentPerformanceEmbedder, PERF_ROWS, TASK_COLS, estimate_cost,
)


@dataclass
class ModelAssignment:
    """One agent → one model assignment for a specific task context."""
    agent_name: str
    model_id: str
    temperature: float
    max_steps: int
    reasoning: str  # Why this model was chosen


@dataclass
class RoutingTable:
    """
    Complete model assignments for all agents.
    This generates the agent section of opencode.json.
    """
    assignments: dict[str, ModelAssignment] = field(default_factory=dict)
    total_estimated_cost_per_sprint: float = 0.0
    accuracy_estimate: float = 0.0

    def to_opencode_agents(self) -> dict:
        """Generate the 'agent' section of opencode.json from routing decisions."""
        agents = {}
        for name, assignment in self.assignments.items():
            agents[name] = {
                "model": assignment.model_id,
                "temperature": assignment.temperature,
                "steps": assignment.max_steps,
            }
        return agents


# Model candidates to evaluate (the search space)
MODEL_CANDIDATES = [
    {"id": "anthropic/claude-sonnet-4-20250514", "tier": "mid",
     "strengths": ["code_gen", "review", "general"], "default_temp": 0.3},
    {"id": "anthropic/claude-haiku-4-20250514", "tier": "fast",
     "strengths": ["test_gen", "docs", "simple_tasks"], "default_temp": 0.2},
    {"id": "anthropic/claude-opus-4-20250514", "tier": "high",
     "strengths": ["physics", "architecture", "complex_reasoning"], "default_temp": 0.1},
    {"id": "openai/gpt-5.1-codex", "tier": "mid",
     "strengths": ["code_gen", "refactoring"], "default_temp": 0.2},
    {"id": "google/gemini-3-pro", "tier": "mid",
     "strengths": ["analysis", "exploration"], "default_temp": 0.3},
]

# Agent → primary task type mapping
AGENT_PRIMARY_TASKS = {
    "architect": "plan",
    "coder": "implement",
    "reviewer": "review",
    "tester": "test",
    "physicist": "physics_check",
    "guardian": "security_audit",
    "documenter": "document",
    "autotuner": "autotune",
    "invariant_runner": "test",
    "retro": "review",
    "strategist": "plan",
}

# Default agent configurations (before any optimization)
DEFAULT_CONFIGS: dict[str, dict] = {
    "architect":        {"model": "anthropic/claude-sonnet-4-20250514", "temp": 0.4, "steps": 15},
    "coder":            {"model": "anthropic/claude-sonnet-4-20250514", "temp": 0.2, "steps": 30},
    "reviewer":         {"model": "anthropic/claude-sonnet-4-20250514", "temp": 0.1, "steps": 10},
    "tester":           {"model": "anthropic/claude-haiku-4-20250514",  "temp": 0.2, "steps": 20},
    "physicist":        {"model": "anthropic/claude-opus-4-20250514",   "temp": 0.1, "steps": 10},
    "guardian":         {"model": "anthropic/claude-sonnet-4-20250514", "temp": 0.1, "steps": 10},
    "documenter":       {"model": "anthropic/claude-haiku-4-20250514",  "temp": 0.3, "steps": 15},
    "autotuner":        {"model": "anthropic/claude-sonnet-4-20250514", "temp": 0.2, "steps": 50},
    "invariant_runner": {"model": "anthropic/claude-haiku-4-20250514",  "temp": 0.0, "steps": 10},
    "retro":            {"model": "anthropic/claude-sonnet-4-20250514", "temp": 0.5, "steps": 10},
    "strategist":       {"model": "anthropic/claude-opus-4-20250514",   "temp": 0.3, "steps": 15},
}


class ModelRouter:
    """
    Routes tasks to optimal models based on accumulated performance data.
    
    Three-tier routing logic:
    
    TIER 1 (cold start, <10 invocations per agent): Use DEFAULT_CONFIGS.
           Population priors. Honest wide CIs. Same as HAPOS cold start.
    
    TIER 2 (warm, 10-50 invocations): Use performance data to select
           from MODEL_CANDIDATES. Pick model with best accuracy-cost ratio.
    
    TIER 3 (hot, >50 invocations): Full optimization across all dimensions.
           The accuracy-cost Pareto frontier is computed per agent×task.
           Model assigned = the one closest to the frontier with lowest cost.
    """
    def __init__(self, metrics: DevMetricsTracker):
        self.metrics = metrics
        self._current_table: Optional[RoutingTable] = None

    def _score_model(self, invocations: list[AgentInvocation],
                     model_id: str, w_acc: float = 0.5,
                     w_eff: float = 0.3, w_cost: float = 0.2) -> float:
        """Score a model for an agent×task pair from historical data."""
        model_invs = [i for i in invocations if i.model_id == model_id]
        if not model_invs:
            return 0.0  # No data — can't score

        acc = sum(i.accuracy for i in model_invs) / len(model_invs)
        eff = sum(i.efficiency for i in model_invs) / len(model_invs)
        avg_cost = sum(i.cost_usd for i in model_invs) / len(model_invs)
        cost_norm = min(1.0, avg_cost * 100)  # Normalize to ~[0,1]

        return w_acc * acc + w_eff * eff - w_cost * cost_norm

    def compute_routing(self) -> RoutingTable:
        """
        Main routing computation. Called weekly (dev Loop 2).
        Produces a complete RoutingTable for all agents.
        """
        table = RoutingTable()

        for agent_name, defaults in DEFAULT_CONFIGS.items():
            invocations = self.metrics.query(agent=agent_name)
            n = len(invocations)

            if n < 10:
                # TIER 1: Cold start — use population priors
                table.assignments[agent_name] = ModelAssignment(
                    agent_name=agent_name,
                    model_id=defaults["model"],
                    temperature=defaults["temp"],
                    max_steps=defaults["steps"],
                    reasoning=f"cold start ({n} invocations): using defaults",
                )
                continue

            # TIER 2/3: Score each candidate model
            best_model = defaults["model"]
            best_score = -1.0
            best_reasoning = ""

            for candidate in MODEL_CANDIDATES:
                score = self._score_model(invocations, candidate["id"])
                if score > best_score:
                    best_score = score
                    best_model = candidate["id"]
                    best_reasoning = (
                        f"scored {score:.3f} from {n} invocations "
                        f"(tier={'hot' if n > 50 else 'warm'})"
                    )

            # Optimize temperature from historical data
            # Lower temp for high-accuracy agents, higher for creative agents
            success_invs = [i for i in invocations if i.outcome == "success"]
            if success_invs:
                best_temp = sum(i.temperature for i in success_invs) / len(success_invs)
            else:
                best_temp = defaults["temp"]

            # Optimize steps from historical data
            if success_invs:
                avg_steps_needed = max(5, int(sum(1 for _ in success_invs) / len(success_invs) * defaults["steps"]))
            else:
                avg_steps_needed = defaults["steps"]

            table.assignments[agent_name] = ModelAssignment(
                agent_name=agent_name,
                model_id=best_model,
                temperature=round(best_temp, 2),
                max_steps=avg_steps_needed,
                reasoning=best_reasoning,
            )

        self._current_table = table
        return table

    def get_model_for(self, agent_name: str) -> ModelAssignment:
        """Real-time routing: which model should this agent use right now?"""
        if self._current_table and agent_name in self._current_table.assignments:
            return self._current_table.assignments[agent_name]
        # Fallback to defaults
        d = DEFAULT_CONFIGS.get(agent_name, DEFAULT_CONFIGS["coder"])
        return ModelAssignment(agent_name, d["model"], d["temp"], d["steps"], "default")
