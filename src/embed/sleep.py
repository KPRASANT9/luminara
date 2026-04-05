"""
Sleep/Circadian Domain Embedder — Kronauer two-process model.
Produces DomainMatrix M_sleep with 5 physics variables × 4 timescale tiers.
Projection W_sleep maps to Φ_E (φ_circ, φ_temp) and Φ_R (φ_sleep, φ_rebound).

Why matrix > scalars:
  Scalar φ_circ=0.87 loses whether that came from aligned-phase-high-amplitude
  or misaligned-phase-coincidental-r. The matrix preserves the full state.
  The projection W decides what matters — and W is optimizable.
"""
from __future__ import annotations
import math
from dataclasses import dataclass
from typing import Optional
from src.core.fos import DomainEmbedder, DomainMatrix, ProjectionMatrix


@dataclass
class KronauerParams:
    tau: float = 24.2
    chi_rise: float = 0.04
    chi_decay: float = 0.02
    alpha_light: float = 0.16
    A_c: float = 0.12
    version: str = "v0.0.0"


class SleepEmbedder(DomainEmbedder):
    """
    Physics: Kronauer (1982) two-process model.
    Structure: FIXED (the ODE). Parameters: EVOLVING (Loop 2).
    Projection: EVOLVING (Embedding Ratchet).
    """
    ROW_LABELS = ["process_s", "process_c", "phase_coherence", "kuramoto_r", "dS_dt"]
    COL_LABELS = ["T1", "T2", "T3", "T4"]

    def __init__(self, params: Optional[KronauerParams] = None,
                 projection: Optional[ProjectionMatrix] = None):
        super().__init__(domain="sleep", targets=["E", "R"], projection=projection)
        self.params = params or KronauerParams()

    def compute_matrix(self, **inputs) -> DomainMatrix:
        """
        Run Kronauer ODE at 4 timescales → 5×4 matrix.
        
        inputs: hours_awake, light_lux, sleep_efficiency,
                hours_awake_t1, hours_awake_t2, hours_awake_t4
        """
        p = self.params
        ha = inputs.get("hours_awake", 14.0)
        lux = inputs.get("light_lux", 100.0)
        eff = inputs.get("sleep_efficiency", 0.85)

        def _kronauer_at(hours: float) -> list[float]:
            s = 1.0 - math.exp(-p.chi_rise * max(0, hours))
            dphase = (2 * math.pi / p.tau) * hours
            phase = dphase % (2 * math.pi)
            c = p.A_c * math.cos(phase)
            ref = (2 * math.pi * hours / 24.0) % (2 * math.pi)
            r = max(0, 1.0 - abs(phase - ref) / math.pi)
            ds_dt = p.chi_rise * (1.0 - s)
            return [s, c, max(0, 1.0 - abs(c)), r, ds_dt]

        # Compute at 4 timescales
        t1_hours = inputs.get("hours_awake_t1", ha * 0.25)
        t2_hours = inputs.get("hours_awake_t2", ha * 0.5)
        t4_hours = inputs.get("hours_awake_t4", ha)

        cols = [_kronauer_at(t1_hours), _kronauer_at(t2_hours),
                _kronauer_at(ha), _kronauer_at(t4_hours)]
        # Transpose: cols[tier][var] → values[var][tier]
        values = [[cols[t][v] for t in range(4)] for v in range(5)]

        return DomainMatrix(
            domain="sleep", row_labels=list(self.ROW_LABELS),
            col_labels=list(self.COL_LABELS), values=values,
            units={"process_s": "dimensionless", "process_c": "dimensionless",
                   "phase_coherence": "dimensionless", "kuramoto_r": "dimensionless",
                   "dS_dt": "1/hour"},
            targets=["E", "R"],
        )

    def default_projection(self) -> ProjectionMatrix:
        """
        Initial W from domain expertise:
          φ_circ      ← mostly kuramoto_r (row 3)
          φ_temp      ← mostly phase_coherence (row 2)
          φ_sleep_q   ← mostly process_s (row 0) decay = good sleep
          φ_rebound   ← mostly process_c (row 1) negative = deep nadir
        
        W ∈ ℝ^(4 × 5):
        """
        return ProjectionMatrix(
            domain="sleep",
            output_labels=["phi_circ", "phi_temp", "phi_sleep_quality", "phi_temp_rebound"],
            input_labels=list(self.ROW_LABELS),
            weights=[
                # process_s, process_c, phase_coh, kuramoto_r, dS_dt
                [0.05,       0.05,      0.10,      0.75,       0.05],   # φ_circ
                [0.05,       0.10,      0.70,      0.10,       0.05],   # φ_temp
                [0.60,       0.05,      0.05,      0.10,       0.20],   # φ_sleep_q
                [0.10,       0.50,      0.20,      0.10,       0.10],   # φ_rebound
            ],
        )

    def get_params(self) -> dict[str, float]:
        p = self.params
        return {"tau": p.tau, "chi_rise": p.chi_rise, "chi_decay": p.chi_decay,
                "alpha_light": p.alpha_light, "A_c": p.A_c}

    def set_params(self, params: dict[str, float]) -> None:
        for k, v in params.items():
            if hasattr(self.params, k): setattr(self.params, k, v)
