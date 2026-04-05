"""
Stage 5: Synthesize — combine all partitions into HAP score.
Input: TwinState (Φ populated by embed, Κ by couple, Π by loop3).
Output: HAPResult with score + gradient + per-sub-score breakdown.

Law II: compositional — sub-scores computed independently, then combined.
Invariant I4: gradient decomposes exactly. verify_i4() MUST pass.
"""
from __future__ import annotations
import math
from dataclasses import dataclass, field
from typing import Optional

@dataclass
class SynthesisWeights:
    w_e: float = 0.30; w_r: float = 0.30; w_c: float = 0.20; w_a: float = 0.20
    def __post_init__(self):
        assert abs(self.w_e + self.w_r + self.w_c + self.w_a - 1.0) < 1e-6

@dataclass
class SubScoreResult:
    score: float
    components: dict[str, float]
    gradient: dict[str, float]

@dataclass
class HAPResult:
    hap: float; energy: SubScoreResult; recovery: SubScoreResult
    cognitive: SubScoreResult; agency: SubScoreResult
    coupling_factor: float; trajectory_adj: float; weights: SynthesisWeights

    def verify_i4(self, eps: float = 1e-4) -> bool:
        """I4: weighted gradient sum = weighted score sum."""
        grad_sum = (
            self.weights.w_e * sum(self.energy.gradient.values()) +
            self.weights.w_r * sum(self.recovery.gradient.values()) +
            self.weights.w_c * sum(self.cognitive.gradient.values()) +
            self.weights.w_a * sum(self.agency.gradient.values())
        )
        score_sum = (
            self.weights.w_e * self.energy.score +
            self.weights.w_r * self.recovery.score +
            self.weights.w_c * self.cognitive.score +
            self.weights.w_a * self.agency.score
        )
        return abs(grad_sum - score_sum) < eps


def _sub(phi: dict, weights: dict) -> SubScoreResult:
    score = sum(weights.get(k, 0) * phi.get(k, 0.5) for k in weights) * 100
    gradient = {k: weights.get(k, 0) * 100 for k in weights}
    return SubScoreResult(score=score, components=phi, gradient=gradient)


def synthesize(phi_e: dict, phi_r: dict, phi_c: dict, phi_a: dict,
               kappa: dict, pi: dict,
               weights: Optional[SynthesisWeights] = None) -> HAPResult:
    w = weights or SynthesisWeights()
    energy = _sub(phi_e, {"phi_hrv": .25, "phi_rhr": .15, "phi_spo2": .10,
                          "phi_temp": .15, "phi_circ": .20, "phi_gluc_stability": .15})
    recovery = _sub(phi_r, {"phi_hrv_recovery": .25, "phi_sleep_quality": .30,
                            "phi_allostatic_load": .20, "phi_temp_rebound": .10,
                            "phi_self_report_energy": .15})
    cognitive = _sub(phi_c, {"pi_cog": .45, "eta_cog": .30, "epsilon_cog": -.25})
    gamma_h_inv = 1.0 - phi_a.get("gamma_H", 0.5)
    phi_a_adj = {**phi_a, "gamma_H_inv": gamma_h_inv}
    agency = _sub(phi_a_adj, {"gamma_eff": .40, "gamma_epi": .30, "gamma_H_inv": .30})
    # Coupling
    if kappa and pi:
        num = sum(pi.get(k, 1.0) * kappa.get(k, 0.5) for k in kappa)
        den = sum(pi.get(k, 1.0) for k in kappa) or 1.0
        cf = num / den
    else: cf = 1.0
    raw = w.w_e * energy.score + w.w_r * recovery.score + w.w_c * cognitive.score + w.w_a * agency.score
    hap = max(0, min(100, raw * cf))
    result = HAPResult(hap=hap, energy=energy, recovery=recovery,
                       cognitive=cognitive, agency=agency,
                       coupling_factor=cf, trajectory_adj=0.0, weights=w)
    assert result.verify_i4(), "I4 VIOLATED"
    return result
