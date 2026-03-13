"""Cognitive Domain Embedder — Neurovisceral integration → DomainMatrix."""
from __future__ import annotations
import math
from src.core.fos import DomainEmbedder, DomainMatrix, ProjectionMatrix

class CognitiveEmbedder(DomainEmbedder):
    ROW_LABELS = ["rmssd_ratio", "hf_power_ratio", "rem_density", "model_improvement", "hrv_deviation"]
    COL_LABELS = ["T1", "T2", "T3", "T4"]

    def __init__(self, projection=None):
        super().__init__("cognitive", ["C"], projection)

    def compute_matrix(self, **inputs) -> DomainMatrix:
        rmssd = inputs.get("rmssd", 42.0)
        baseline = inputs.get("rmssd_baseline", 45.0)
        hf = inputs.get("hf_power", 300.0)
        hf_base = inputs.get("hf_baseline", 350.0)
        rem = inputs.get("rem_pct", 0.22)
        improvement = inputs.get("model_improvement_rate", 0.0)
        ratio_r = min(1.0, rmssd / max(1, baseline))
        ratio_h = min(1.0, hf / max(1, hf_base))
        dev = max(0, (baseline - rmssd) / max(1, baseline))
        values = [[ratio_r]*4, [ratio_h]*4, [rem/0.25]*4,
                  [min(1, improvement)]*4, [dev]*4]
        return DomainMatrix("cognitive", list(self.ROW_LABELS), list(self.COL_LABELS),
                            values, targets=["C"])

    def default_projection(self) -> ProjectionMatrix:
        return ProjectionMatrix("cognitive", ["pi_cog", "eta_cog", "epsilon_cog"],
                                list(self.ROW_LABELS),
                                [[0.40, 0.40, 0.05, 0.05, 0.10],
                                 [0.05, 0.05, 0.50, 0.35, 0.05],
                                 [0.10, 0.10, 0.05, 0.05, 0.70]])

    def get_params(self) -> dict[str, float]: return {}
    def set_params(self, params) -> None: pass
