"""Agency Domain Embedder — Active inference → DomainMatrix."""
from __future__ import annotations
from src.core.fos import DomainEmbedder, DomainMatrix, ProjectionMatrix
from src.core.types import PredictionLogEntry

class AgencyEmbedder(DomainEmbedder):
    ROW_LABELS = ["pred_alignment", "info_gain_rate", "timing_entropy", "compliance_rate", "goal_delta"]
    COL_LABELS = ["T1", "T2", "T3", "T4"]

    def __init__(self, projection=None):
        super().__init__("agency", ["A"], projection)

    def compute_matrix(self, **inputs) -> DomainMatrix:
        preds = inputs.get("predictions", [])
        entropy = inputs.get("timing_entropy", 0.5)
        closed = [p for p in preds if p.actual_value is not None]
        alignment = 0.5
        if closed:
            als = [1.0 - min(1.0, abs(p.prediction_error or 0)/max(1, abs(p.predicted_value)))
                   for p in closed]
            alignment = sum(als)/len(als)
        values = [[alignment]*4, [0.5]*4, [entropy]*4, [0.5]*4, [0.5]*4]
        return DomainMatrix("agency", list(self.ROW_LABELS), list(self.COL_LABELS),
                            values, targets=["A"])

    def default_projection(self) -> ProjectionMatrix:
        return ProjectionMatrix("agency",
            ["gamma_eff", "gamma_epi", "gamma_H"],
            list(self.ROW_LABELS),
            [[0.60, 0.10, 0.05, 0.15, 0.10],
             [0.10, 0.60, 0.05, 0.15, 0.10],
             [0.05, 0.05, 0.80, 0.05, 0.05]])

    def get_params(self) -> dict[str, float]: return {}
    def set_params(self, params) -> None: pass
