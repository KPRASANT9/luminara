"""Recovery Domain Embedder — McEwen + PID + Banister → DomainMatrix."""
from __future__ import annotations
import math
from dataclasses import dataclass
from typing import Optional
from src.core.fos import DomainEmbedder, DomainMatrix, ProjectionMatrix

@dataclass
class RecoveryParams:
    kp: float = 1.0; ki: float = 0.1; kd: float = 0.05
    k1: float = 1.0; k2: float = 2.0; tau1: float = 45.0; tau2: float = 15.0
    allostatic_sigma: float = 1.0; version: str = "v0.0.0"

class RecoveryEmbedder(DomainEmbedder):
    ROW_LABELS = ["pid_gain", "allostatic_load_inv", "banister_perf", "sleep_arch", "temp_rebound"]
    COL_LABELS = ["T1", "T2", "T3", "T4"]

    def __init__(self, params: Optional[RecoveryParams] = None,
                 projection: Optional[ProjectionMatrix] = None):
        super().__init__("recovery", ["R"], projection)
        self.params = params or RecoveryParams()

    def compute_matrix(self, **inputs) -> DomainMatrix:
        p = self.params
        hrv_current = inputs.get("hrv_current", 40.0)
        hrv_baseline = inputs.get("hrv_baseline", 45.0)
        bio_devs = inputs.get("biomarker_deviations", [])
        sleep_deep = inputs.get("sleep_deep_pct", 0.2)
        sleep_rem = inputs.get("sleep_rem_pct", 0.22)
        sleep_eff = inputs.get("sleep_efficiency", 0.85)
        temp_nadir = inputs.get("temp_nadir", 0.5)
        temp_rise = inputs.get("temp_rise", 0.5)
        loads = inputs.get("training_loads", [])
        days = inputs.get("training_days", [])

        # PID recovery speed
        error = hrv_baseline - hrv_current
        pid = 1.0 / (1.0 + math.exp(-(p.kp * error) / 10.0))
        # McEwen allostatic load (inverted)
        load_inv = 1.0 - (sum(1 for d in bio_devs if abs(d) > p.allostatic_sigma)/max(1,len(bio_devs))) if bio_devs else 0.5
        # Banister
        if loads and days:
            fit = sum(p.k1 * w * math.exp(-d/p.tau1) for w,d in zip(loads, days))
            fat = sum(p.k2 * w * math.exp(-d/p.tau2) for w,d in zip(loads, days))
            ban = 1.0 / (1.0 + math.exp(-(fit - fat)))
        else:
            ban = 0.5
        sleep_q = sleep_deep * 0.5 + sleep_rem * 0.3 + sleep_eff * 0.2
        rebound = temp_nadir * 0.6 + temp_rise * 0.4

        # Single-tier simplified (expand to multi-tier in Phase 1+)
        values = [[v]*4 for v in [pid, load_inv, ban, sleep_q, rebound]]
        return DomainMatrix("recovery", list(self.ROW_LABELS), list(self.COL_LABELS),
                            values, targets=["R"])

    def default_projection(self) -> ProjectionMatrix:
        return ProjectionMatrix("recovery",
            ["phi_hrv_recovery", "phi_allostatic_load", "phi_sleep_quality",
             "phi_temp_rebound", "phi_self_report_energy"],
            list(self.ROW_LABELS),
            [[0.80, 0.05, 0.05, 0.05, 0.05],
             [0.05, 0.80, 0.05, 0.05, 0.05],
             [0.05, 0.05, 0.05, 0.80, 0.05],
             [0.05, 0.05, 0.05, 0.05, 0.80],
             [0.10, 0.20, 0.30, 0.20, 0.20]])

    def get_params(self) -> dict[str, float]:
        p = self.params
        return {"kp": p.kp, "ki": p.ki, "kd": p.kd, "k1": p.k1,
                "k2": p.k2, "tau1": p.tau1, "tau2": p.tau2}
    def set_params(self, params: dict[str, float]) -> None:
        for k, v in params.items():
            if hasattr(self.params, k): setattr(self.params, k, v)
