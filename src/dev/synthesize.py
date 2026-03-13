"""
Dev Substrate Synthesizer — Unified Development Objective.

F_dev(t) = α · N_dev(t) + β · I_dev(t)

N_dev = development negentropy: how much working code is being produced NOW.
        Computed from the four dev sub-scores across all agents.

I_dev = development info gain: how much the agent swarm is LEARNING.
        Computed from accuracy improvement rate across Loop 2 cycles.

This is the SAME equation as HAPOS:
  HAPOS:    F(t) = α · N(t) + β · I(t)    where N = physiological capacity, I = model learning
  DevHAPOS: F(t) = α · N_dev(t) + β · I_dev(t) where N = code production, I = agent learning

The sub-scores and physics change. The equation and the loops don't.
"""
from __future__ import annotations
from dataclasses import dataclass, field
from src.dev.physics import (
    DevMetricsTracker, AgentPerformanceEmbedder, DevSubScore, PERF_ROWS, TASK_COLS,
)
from src.dev.loops import DevConfigVersion
from src.core.fos import DomainMatrix, ObjectiveResult


@dataclass
class DevObjective:
    """Complete development objective computation."""
    f_dev: float                         # Unified score [0, 100]
    n_dev: float                         # Negentropy: working code production
    i_dev: float                         # Info gain: agent learning rate
    alpha: float = 0.7
    beta: float = 0.3
    per_agent_sub_scores: dict[str, dict[str, float]] = field(default_factory=dict)
    agent_matrices: dict[str, DomainMatrix] = field(default_factory=dict)
    meta_gradient: dict[str, float] = field(default_factory=dict)


# Import at function level to avoid circular imports
DEFAULT_CONFIGS_KEYS = [
    "architect", "coder", "reviewer", "tester", "physicist",
    "guardian", "documenter", "autotuner", "invariant_runner",
    "retro", "strategist",
]


def compute_dev_objective(
    metrics: DevMetricsTracker,
    loop2_history: list[DevConfigVersion],
    alpha: float = 0.7,
    beta: float = 0.3,
) -> DevObjective:
    """
    Compute F_dev(t) = α · N_dev(t) + β · I_dev(t).
    
    N_dev: Weighted mean of all agent accuracy scores (how well the swarm is working).
    I_dev: Accuracy improvement rate between last two Loop 2 cycles.
    """
    all_invocations = metrics.query()

    # Compute DomainMatrix per agent
    agent_matrices: dict[str, DomainMatrix] = {}
    per_agent_sub: dict[str, dict[str, float]] = {}

    for agent_name in DEFAULT_CONFIGS_KEYS:
        embedder = AgentPerformanceEmbedder(agent_name)
        matrix, phi = embedder.embed(invocations=all_invocations)
        agent_matrices[agent_name] = matrix
        per_agent_sub[agent_name] = phi

    # N_dev: aggregate accuracy across all agents (negentropy = working code production)
    all_accuracy = [sub.get("A_dev", 0.5) for sub in per_agent_sub.values()]
    all_efficiency = [sub.get("E_dev", 0.5) for sub in per_agent_sub.values()]
    all_coherence = [sub.get("C_dev", 0.5) for sub in per_agent_sub.values()]
    all_velocity = [sub.get("V_dev", 0.5) for sub in per_agent_sub.values()]

    n_dev = (
        0.35 * (sum(all_accuracy) / max(1, len(all_accuracy)))
        + 0.25 * (sum(all_efficiency) / max(1, len(all_efficiency)))
        + 0.25 * (sum(all_coherence) / max(1, len(all_coherence)))
        + 0.15 * (sum(all_velocity) / max(1, len(all_velocity)))
    ) * 100

    # I_dev: how much are agents LEARNING? (accuracy improvement rate)
    i_dev = 0.0
    if len(loop2_history) >= 2:
        curr = loop2_history[-1]
        prev = loop2_history[-2]
        gains = []
        for agent_name in DEFAULT_CONFIGS_KEYS:
            curr_a = curr.per_agent_accuracy.get(agent_name, 0.5)
            prev_a = prev.per_agent_accuracy.get(agent_name, 0.5)
            if prev_a > 0:
                gains.append(max(0, (curr_a - prev_a) / prev_a))
        i_dev = (sum(gains) / max(1, len(gains))) * 100 if gains else 0.0

    # Meta-gradient: per-agent acceleration
    meta_gradient: dict[str, float] = {}
    if len(loop2_history) >= 2:
        for agent_name in DEFAULT_CONFIGS_KEYS:
            curr_a = loop2_history[-1].per_agent_accuracy.get(agent_name, 0.5)
            prev_a = loop2_history[-2].per_agent_accuracy.get(agent_name, 0.5)
            meta_gradient[agent_name] = curr_a - prev_a

    f_dev = alpha * n_dev + beta * i_dev

    return DevObjective(
        f_dev=max(0, min(100, f_dev)),
        n_dev=n_dev, i_dev=i_dev,
        alpha=alpha, beta=beta,
        per_agent_sub_scores=per_agent_sub,
        agent_matrices=agent_matrices,
        meta_gradient=meta_gradient,
    )
