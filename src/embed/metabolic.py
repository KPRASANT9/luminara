"""Metabolic Domain Embedder — Bergman minimal model → DomainMatrix."""
from __future__ import annotations
import math
from dataclasses import dataclass
from typing import Optional
from src.core.fos import DomainEmbedder, DomainMatrix, ProjectionMatrix

@dataclass
class BergmanParams:
    S_G: float = 0.01; S_I: float = 0.0005; G_b: float = 90.0; version: str = "v0.0.0"

class MetabolicEmbedder(DomainEmbedder):
    ROW_LABELS = ["glucose_mean", "glucose_cv", "insulin_sensitivity", "time_in_range"]
    COL_LABELS = ["T1", "T2", "T3", "T4"]

    def __init__(self, params: Optional[BergmanParams] = None,
                 projection: Optional[ProjectionMatrix] = None):
        super().__init__("metabolic", ["E"], projection)
        self.params = params or BergmanParams()

    def compute_matrix(self, **inputs) -> DomainMatrix:
        glucose_vals = inputs.get("glucose_values", [])
        if not glucose_vals:
            # Graceful degradation: neutral matrix
            return DomainMatrix("metabolic", list(self.ROW_LABELS), list(self.COL_LABELS),
                                [[0.5]*4 for _ in range(4)], targets=["E"])
        n = len(glucose_vals)
        mean_g = sum(glucose_vals) / n
        sigma = math.sqrt(sum((v-mean_g)**2 for v in glucose_vals)/n) if n > 1 else 0
        cv = sigma / mean_g if mean_g > 0 else 0
        tir = sum(1 for v in glucose_vals if 70 <= v <= 140) / n
        si = min(1.0, self.params.S_I * 1000)
        # Simplified multi-tier: divide into quartiles
        q = max(1, n // 4)
        tiers = [glucose_vals[:q], glucose_vals[q:2*q], glucose_vals[2*q:3*q], glucose_vals[3*q:]]
        values = []
        for row_fn in [lambda vs: sum(vs)/len(vs)/200 if vs else 0.5,
                       lambda vs: (math.sqrt(sum((v-sum(vs)/len(vs))**2 for v in vs)/len(vs))/(sum(vs)/len(vs))) if vs and sum(vs)>0 else 0.5,
                       lambda _: si,
                       lambda vs: sum(1 for v in vs if 70<=v<=140)/len(vs) if vs else 0.5]:
            values.append([row_fn(t) for t in tiers])
        return DomainMatrix("metabolic", list(self.ROW_LABELS), list(self.COL_LABELS),
                            values, units={"glucose_mean":"mg/dL","glucose_cv":"ratio"}, targets=["E"])

    def default_projection(self) -> ProjectionMatrix:
        return ProjectionMatrix("metabolic", ["phi_gluc_stability"],
                                list(self.ROW_LABELS),
                                [[0.15, -0.40, 0.20, 0.50]])  # Low CV + high TIR = stable

    def get_params(self) -> dict[str, float]:
        return {"S_G": self.params.S_G, "S_I": self.params.S_I, "G_b": self.params.G_b}
    def set_params(self, params: dict[str, float]) -> None:
        for k, v in params.items():
            if hasattr(self.params, k): setattr(self.params, k, v)
