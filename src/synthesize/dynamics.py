"""
Dynamics (Δ) Vector — 1st and 2nd derivatives of Φ at T3/T4/T5.

v6: "Position without velocity is half the physics."
The Δ vector tells the system not just WHERE you are (Φ), but
WHICH DIRECTION you're moving (dΦ/dt) and WHETHER THAT'S CHANGING (d²Φ/dt²).

The meta-gradient ∇²HAP is computed from Δ at T4/T5 timescales
and fed to Loop 2 + Loop 3 to guide model/precision updates.

Laws: Law II (compositional — Δ partitioned like Φ), Law IV (feeds loops)
Invariants: I4 (gradient decomposable through dynamics)
"""
from __future__ import annotations
from dataclasses import dataclass, field
from src.core.types import TwinState, SubScore


@dataclass
class DynamicsVector:
    """
    Δ(t) — the derivative component of S(t).
    Partitioned by sub-score, like Φ.
    """
    d_phi: dict[str, dict[str, float]] = field(default_factory=dict)
    # {"E": {"phi_hrv": 0.02, ...}, "R": {...}, ...}   — 1st derivative per component
    d2_phi: dict[str, dict[str, float]] = field(default_factory=dict)
    # {"E": {"phi_hrv": -0.001, ...}, ...}              — 2nd derivative
    tier: str = "T3"  # Which timescale tier these derivatives are at


def compute_dynamics(
    current: TwinState,
    previous: TwinState,
    dt_hours: float = 24.0,
    previous_dynamics: DynamicsVector | None = None,
) -> DynamicsVector:
    """
    Compute 1st and 2nd derivatives of all Φ components.
    
    1st derivative: dΦ/dt = (Φ(t) - Φ(t-1)) / dt
    2nd derivative: d²Φ/dt² = (dΦ/dt(t) - dΦ/dt(t-1)) / dt
    
    Called at T3 (daily), T4 (weekly), T5 (monthly).
    Higher tiers use averages of lower-tier derivatives.
    """
    delta = DynamicsVector()
    
    for sub in SubScore:
        s = sub.value
        curr_phi = current.get_phi(sub).components
        prev_phi = previous.get_phi(sub).components
        
        # 1st derivative
        d1: dict[str, float] = {}
        for k in curr_phi:
            if k in prev_phi:
                d1[k] = (curr_phi[k] - prev_phi[k]) / dt_hours
            else:
                d1[k] = 0.0
        delta.d_phi[s] = d1
        
        # 2nd derivative (requires previous dynamics)
        d2: dict[str, float] = {}
        if previous_dynamics and s in previous_dynamics.d_phi:
            prev_d1 = previous_dynamics.d_phi[s]
            for k in d1:
                if k in prev_d1:
                    d2[k] = (d1[k] - prev_d1[k]) / dt_hours
                else:
                    d2[k] = 0.0
        else:
            d2 = {k: 0.0 for k in d1}
        delta.d2_phi[s] = d2
    
    return delta


def extract_meta_gradient(dynamics_t4: DynamicsVector) -> dict[str, float]:
    """
    Extract per-sub-score acceleration signal for Loop 2 + Loop 3.
    
    Positive = this sub-score's components are accelerating improvement.
    Negative = decelerating. Zero = plateau.
    
    v6 Step 6: "∇²HAP = d(∇HAP)/dt at T4/T5 → feeds Loop 2 + Loop 3"
    """
    acceleration: dict[str, float] = {}
    for sub in ["E", "R", "C", "A"]:
        d2 = dynamics_t4.d2_phi.get(sub, {})
        if d2:
            acceleration[sub] = sum(d2.values()) / len(d2)
        else:
            acceleration[sub] = 0.0
    return acceleration
