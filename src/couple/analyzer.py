"""
Stage 4: Couple — compute cross-domain Κ vector.
Input: TwinState with Φ_E, Φ_R, Φ_C, Φ_A populated by embed stage.
Output: Κ dict with all 7 canonical pairs.

Law II: coupling is first-class state, not derived after the fact.
Invariant I3: ALL 7 pairs must return a value. Neutral 0.5 if insufficient data.
"""
from __future__ import annotations
from src.core.types import TwinState, SubScore

PAIRS = ["E_E", "E_R", "R_R", "R_E", "C_A", "E_C", "ALL"]

def compute_kappa(twin: TwinState) -> dict[str, float]:
    """
    Pure function. TwinState in, 7-pair Κ dict out.
    I3 enforced: result always has exactly 7 keys.
    """
    kappa: dict[str, float] = {}

    def _coherence(phi) -> float:
        vals = list(phi.components.values())
        if len(vals) < 2: return 0.5
        m = sum(vals) / len(vals)
        v = sum((x - m) ** 2 for x in vals) / len(vals)
        return max(0.0, min(1.0, 1.0 - v))

    def _cross(phi_a, phi_b) -> float:
        a = list(phi_a.components.values())
        b = list(phi_b.components.values())
        if not a or not b: return 0.5
        return min(1.0, (sum(a) / len(a) + sum(b) / len(b)) / 2.0)

    kappa["E_E"] = _coherence(twin.phi_e)
    kappa["R_R"] = _coherence(twin.phi_r)
    kappa["E_R"] = _cross(twin.phi_e, twin.phi_r)
    kappa["R_E"] = _cross(twin.phi_r, twin.phi_e)
    kappa["C_A"] = _cross(twin.phi_c, twin.phi_a)
    kappa["E_C"] = _cross(twin.phi_e, twin.phi_c)
    kappa["ALL"] = sum(kappa.values()) / len(kappa) if kappa else 0.5

    assert len(kappa) == 7, f"I3 VIOLATED: {len(kappa)}/7 pairs"
    return kappa
