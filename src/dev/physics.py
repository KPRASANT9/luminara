"""
F-OS Development Substrate — Physics of Agentic Coding.

Just as HAPOS models a human as a thermodynamic system minimizing F,
this module models the agent swarm as an information-processing system
minimizing DEVELOPMENT free energy: the gap between what agents believe
will work and what actually produces invariant-passing code.

THE FOUR GOVERNING EQUATIONS (prepare.py — immutable):

  1. Token Conservation: cost = Σ(tokens × price). Can't compute for free.
  2. Context Window Bound: H(output) ≤ C(context). Info that doesn't fit is lost.
  3. Accuracy-Cost Frontier: Pareto boundary between pass_rate and token_spend.
  4. Compositional Correctness: pipeline correct iff each stage correct AND contracts hold.

THE FOUR DEV SUB-SCORES:

  Accuracy  (A_dev): Does the agent produce invariant-passing code?
  Efficiency (E_dev): How many tokens per invariant-passing commit?
  Coherence (C_dev): Does the agent maintain architectural consistency?
  Velocity  (V_dev): How fast does the system produce working code?

DOMAIN MATRIX PER AGENT:
  Each agent gets M_agent ∈ ℝ^(k × n) where:
    k = performance dimensions (accuracy, efficiency, coherence, velocity, token_cost)
    n = task types the agent handles (plan, implement, review, test, physics_check, ...)
  
  ProjectionMatrix W_agent maps M_agent into the four dev sub-scores.
  W_agent is optimized by the Prompt Ratchet (Level 2).
  The performance data (M_agent content) is observed from real outcomes (Level 1).
  The agent topology and routing is optimized by Loop 3.
"""
from __future__ import annotations
import uuid, math, time
from dataclasses import dataclass, field
from datetime import datetime, timezone
from enum import Enum
from typing import Optional
from src.core.fos import DomainMatrix, ProjectionMatrix, DomainEmbedder, RatchetExperiment


# ═══════════════════════════════════════════════════════════════
# DEV SUB-SCORES
# ═══════════════════════════════════════════════════════════════

class DevSubScore(str, Enum):
    ACCURACY = "A_dev"     # Invariant pass rate
    EFFICIENCY = "E_dev"   # Tokens per successful commit
    COHERENCE = "C_dev"    # Pattern adherence + contract satisfaction
    VELOCITY = "V_dev"     # Time to invariant-passing commit


# ═══════════════════════════════════════════════════════════════
# DEV METRICS — the "sensor" for the development substrate
# ═══════════════════════════════════════════════════════════════

@dataclass
class AgentInvocation:
    """
    One agent call. This is the CloudEvent of the dev substrate.
    Law I: every invocation is logged immutably.
    """
    id: str = field(default_factory=lambda: str(uuid.uuid4()))
    ts: datetime = field(default_factory=lambda: datetime.now(timezone.utc))
    agent_name: str = ""           # "coder", "reviewer", "physicist", etc.
    model_id: str = ""             # "anthropic/claude-sonnet-4-20250514"
    task_type: str = ""            # "implement", "review", "test", "plan", ...
    tokens_in: int = 0
    tokens_out: int = 0
    cost_usd: float = 0.0
    duration_s: float = 0.0
    temperature: float = 0.0
    max_steps: int = 0
    # Outcome (filled after task completes)
    invariants_passed: int = 0
    invariants_total: int = 8
    pattern_adherence: float = 0.0  # 0-1: does output follow coding standards?
    contract_satisfied: bool = True  # Does output match IO contract?
    outcome: str = "pending"        # "success" | "partial" | "failure" | "crash"

    @property
    def accuracy(self) -> float:
        """A_dev component: invariant pass rate."""
        return self.invariants_passed / max(1, self.invariants_total)

    @property
    def efficiency(self) -> float:
        """E_dev component: inverse token cost, normalized."""
        total_tokens = self.tokens_in + self.tokens_out
        if total_tokens == 0: return 1.0
        if self.outcome != "success": return 0.0
        return min(1.0, 1000.0 / total_tokens)  # 1000 tokens for success = perfect

    @property
    def coherence(self) -> float:
        """C_dev component: pattern adherence + contract."""
        return (self.pattern_adherence * 0.6 +
                (1.0 if self.contract_satisfied else 0.0) * 0.4)

    @property
    def velocity(self) -> float:
        """V_dev component: inverse time, normalized."""
        if self.duration_s <= 0: return 0.5
        return min(1.0, 60.0 / self.duration_s)  # 60s = perfect velocity


class DevMetricsTracker:
    """
    The canonical store for all agent invocations.
    Analogous to TimescaleDB for HAPOS: immutable log, queryable by agent/task/time.
    """
    def __init__(self):
        self._invocations: list[AgentInvocation] = []

    def record(self, inv: AgentInvocation) -> None:
        self._invocations.append(inv)

    def query(self, agent: str = "", task: str = "",
              since: Optional[datetime] = None) -> list[AgentInvocation]:
        results = self._invocations
        if agent: results = [i for i in results if i.agent_name == agent]
        if task: results = [i for i in results if i.task_type == task]
        if since: results = [i for i in results if i.ts >= since]
        return results

    @property
    def total_cost_usd(self) -> float:
        return sum(i.cost_usd for i in self._invocations)

    @property
    def total_tokens(self) -> int:
        return sum(i.tokens_in + i.tokens_out for i in self._invocations)


# ═══════════════════════════════════════════════════════════════
# AGENT PERFORMANCE MATRIX — DomainMatrix for each agent
# ═══════════════════════════════════════════════════════════════

# Performance dimensions (rows of the domain matrix)
PERF_ROWS = ["accuracy", "efficiency", "coherence", "velocity", "token_cost"]

# Task types (columns of the domain matrix)
TASK_COLS = ["plan", "implement", "review", "test", "physics_check",
             "security_audit", "document", "autotune"]


class AgentPerformanceEmbedder(DomainEmbedder):
    """
    Computes the DomainMatrix for one agent from its invocation history.
    
    This IS the embed stage for the dev substrate:
      AgentInvocations (raw signals) → AgentPerformanceMatrix (structured M_d)
      → ProjectionMatrix W_d → Dev sub-score components
    
    The matrix M_agent ∈ ℝ^(5 × 8):
      Rows: accuracy, efficiency, coherence, velocity, token_cost
      Cols: plan, implement, review, test, physics_check, security_audit, document, autotune
    
    Each cell = rolling mean from the last 30 invocations of that agent×task pair.
    Empty cells = 0.5 (graceful degradation).
    
    The ProjectionMatrix W maps this into dev sub-scores:
      A_dev = f(accuracy row, coherence row)
      E_dev = f(efficiency row, token_cost row)
      C_dev = f(coherence row, accuracy row)
      V_dev = f(velocity row, efficiency row)
    """
    def __init__(self, agent_name: str,
                 projection: Optional[ProjectionMatrix] = None):
        super().__init__(
            domain=f"agent_{agent_name}",
            targets=["A_dev", "E_dev", "C_dev", "V_dev"],
            projection=projection,
        )
        self.agent_name = agent_name

    def compute_matrix(self, **inputs) -> DomainMatrix:
        """
        Compute performance matrix from invocation history.
        inputs: {"invocations": list[AgentInvocation]}
        """
        invocations: list[AgentInvocation] = inputs.get("invocations", [])
        mine = [i for i in invocations if i.agent_name == self.agent_name]

        values: list[list[float]] = []
        for row_name in PERF_ROWS:
            row: list[float] = []
            for col_name in TASK_COLS:
                matches = [i for i in mine if i.task_type == col_name][-30:]
                if not matches:
                    row.append(0.5)  # Graceful degradation
                    continue
                if row_name == "accuracy":
                    row.append(sum(i.accuracy for i in matches) / len(matches))
                elif row_name == "efficiency":
                    row.append(sum(i.efficiency for i in matches) / len(matches))
                elif row_name == "coherence":
                    row.append(sum(i.coherence for i in matches) / len(matches))
                elif row_name == "velocity":
                    row.append(sum(i.velocity for i in matches) / len(matches))
                elif row_name == "token_cost":
                    total = sum(i.tokens_in + i.tokens_out for i in matches)
                    row.append(min(1.0, total / max(1, len(matches)) / 10000))
            values.append(row)

        return DomainMatrix(
            domain=f"agent_{self.agent_name}",
            row_labels=list(PERF_ROWS),
            col_labels=list(TASK_COLS),
            values=values,
            units={"accuracy": "ratio", "efficiency": "ratio",
                   "coherence": "ratio", "velocity": "ratio",
                   "token_cost": "normalized_tokens"},
            targets=["A_dev", "E_dev", "C_dev", "V_dev"],
        )

    def default_projection(self) -> ProjectionMatrix:
        """
        Initial W: maps 5 performance rows → 4 dev sub-scores.
        W ∈ ℝ^(4 × 5).
        
        A_dev ← mostly accuracy + some coherence
        E_dev ← mostly efficiency + inverse token_cost
        C_dev ← mostly coherence + some accuracy
        V_dev ← mostly velocity + some efficiency
        """
        return ProjectionMatrix(
            domain=f"agent_{self.agent_name}",
            output_labels=["A_dev", "E_dev", "C_dev", "V_dev"],
            input_labels=list(PERF_ROWS),
            weights=[
                # accuracy, efficiency, coherence, velocity, token_cost
                [0.60,     0.05,       0.25,      0.05,     0.05],   # A_dev
                [0.05,     0.50,       0.05,      0.10,    -0.30],   # E_dev (negative token_cost)
                [0.20,     0.05,       0.60,      0.05,     0.10],   # C_dev
                [0.05,     0.20,       0.05,      0.60,     0.10],   # V_dev
            ],
        )

    def get_params(self) -> dict[str, float]:
        """Agent config params readable by Loop 2."""
        return {}  # Populated by model router

    def set_params(self, params: dict[str, float]) -> None:
        pass  # Config written via opencode.json


# ═══════════════════════════════════════════════════════════════
# MODEL PRICING (for token conservation equation)
# ═══════════════════════════════════════════════════════════════

MODEL_PRICING: dict[str, dict[str, float]] = {
    # model_id: {"input": $/1M tokens, "output": $/1M tokens}
    "anthropic/claude-sonnet-4-20250514": {"input": 3.0, "output": 15.0},
    "anthropic/claude-haiku-4-20250514": {"input": 0.80, "output": 4.0},
    "anthropic/claude-opus-4-20250514": {"input": 15.0, "output": 75.0},
    "openai/gpt-5.1-codex": {"input": 2.0, "output": 8.0},
    "google/gemini-3-pro": {"input": 1.25, "output": 5.0},
}

def estimate_cost(model_id: str, tokens_in: int, tokens_out: int) -> float:
    """Token Conservation equation: cost = Σ(tokens × price)."""
    pricing = MODEL_PRICING.get(model_id, {"input": 3.0, "output": 15.0})
    return (tokens_in * pricing["input"] + tokens_out * pricing["output"]) / 1_000_000


# ═══════════════════════════════════════════════════════════════
# DEV PREDICTION LOG — tracks agent predictions vs outcomes
# ═══════════════════════════════════════════════════════════════

@dataclass
class DevPredictionEntry:
    """
    Every agent implicitly predicts: "my output will pass invariants."
    This entry tracks that prediction against reality.
    Law IV: feeds dev Loop 2.
    """
    id: str = field(default_factory=lambda: str(uuid.uuid4()))
    agent_name: str = ""
    model_id: str = ""
    task_type: str = ""
    predicted_pass: bool = True     # Agent always predicts success
    actual_pass: bool = False       # Did invariants actually pass?
    prediction_error: float = 0.0   # 0 = correct prediction, 1 = wrong
    tokens_consumed: int = 0
    cost_usd: float = 0.0
    config_snapshot: dict = field(default_factory=dict)  # model, temp, steps at time of invocation

    def close(self, actual_pass: bool) -> None:
        self.actual_pass = actual_pass
        self.prediction_error = 0.0 if self.predicted_pass == actual_pass else 1.0
