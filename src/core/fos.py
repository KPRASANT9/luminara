"""
F-OS: Free-Energy Operating System — Universal Base Framework.

HAPOS is F-OS instantiated for human physiology.
Any domain with governing equations can instantiate F-OS.

The separation:
  FIXED (prepare.py): Physics structure, invariant definitions
  EVOLVING (train.py): Parameters, embedding projections, composition weights
  GUIDING (program.md): Objectives, goals, search strategy
"""
from __future__ import annotations
import uuid, math
from abc import ABC, abstractmethod
from dataclasses import dataclass, field
from datetime import datetime, timezone
from typing import Any, Optional


@dataclass(frozen=True)
class Signal:
    """Universal immutable observation. Law I: cannot be destroyed."""
    id: str = field(default_factory=lambda: str(uuid.uuid4()))
    source: str = ""
    type: str = ""
    subject: str = ""
    time: datetime = field(default_factory=lambda: datetime.now(timezone.utc))
    value: float | dict[str, Any] = 0.0
    unit: str = ""
    confidence: float = 1.0
    targets: tuple[str, ...] = ()
    raw: Optional[dict[str, Any]] = None
    def __post_init__(self):
        assert self.type and self.subject and len(self.targets) > 0


@dataclass
class DomainMatrix:
    """
    Structured embedding: M_d ∈ ℝ^(k × n).
    k = physics state variables (rows), n = timescale tiers (columns).
    Preserves structure that scalars destroy. Each entry has units.
    
    Example (sleep): rows=[process_s, process_c, phase, kuramoto_r, dS_dt]
                     cols=[T1, T2, T3, T4]
    """
    domain: str
    row_labels: list[str] = field(default_factory=list)
    col_labels: list[str] = field(default_factory=list)
    values: list[list[float]] = field(default_factory=list)
    units: dict[str, str] = field(default_factory=dict)
    targets: list[str] = field(default_factory=list)

    @property
    def k(self) -> int: return len(self.row_labels)
    @property
    def n(self) -> int: return len(self.col_labels)

    def row_means(self) -> list[float]:
        return [sum(r)/len(r) if r else 0.0 for r in self.values]

    def row_sigmas(self) -> list[float]:
        """Temporal variability per variable. Feeds I2."""
        out = []
        for row in self.values:
            if len(row) < 2: out.append(0.0); continue
            m = sum(row)/len(row)
            out.append(math.sqrt(sum((v-m)**2 for v in row)/len(row)))
        return out


@dataclass
class ProjectionMatrix:
    """
    W_d ∈ ℝ^(m × k): maps domain matrix into Φ components.
    m = output Φ components, k = physics state variables.
    Optimized by Embedding Ratchet. Physics (M_d) stays fixed.
    
    Three optimization levels:
      L1 (Loop 2): params → M_d content changes
      L2 (Embedding Ratchet): W_d → projection changes
      L3 (Loop 3): Π → composition changes
    """
    domain: str
    output_labels: list[str]
    input_labels: list[str]
    weights: list[list[float]]
    version: str = "w0.0.0"

    @property
    def m(self) -> int: return len(self.output_labels)
    @property
    def k_in(self) -> int: return len(self.input_labels)

    def project(self, matrix: DomainMatrix) -> dict[str, float]:
        """Φ_d = W_d @ row_means(M_d). Returns {component: value} dict."""
        means = matrix.row_means()
        assert len(means) == self.k_in, f"Shape mismatch: M={len(means)}, W={self.k_in}"
        result = {}
        for i, label in enumerate(self.output_labels):
            val = sum(self.weights[i][j] * means[j] for j in range(self.k_in))
            result[label] = max(0.0, min(1.0, val))
        return result

    def perturb(self, scale: float = 0.02) -> ProjectionMatrix:
        """Create perturbed copy for Embedding Ratchet search."""
        import random
        new_w = [[w + random.gauss(0, scale) for w in row] for row in self.weights]
        return ProjectionMatrix(self.domain, list(self.output_labels),
                                list(self.input_labels), new_w,
                                f"w-{uuid.uuid4().hex[:6]}")


class DomainEmbedder(ABC):
    """
    Abstract base for all domain embedders across any F-OS instantiation.
    
    Subclass implements: compute_matrix() (physics), default_projection() (W_d init).
    Base handles: projection, embed pipeline, param access.
    """
    def __init__(self, domain: str, targets: list[str],
                 projection: Optional[ProjectionMatrix] = None):
        self.domain = domain
        self.targets = targets
        self._projection = projection

    @abstractmethod
    def compute_matrix(self, **inputs) -> DomainMatrix:
        """Physics equations → structured domain matrix. Pure function."""
        ...

    @abstractmethod
    def default_projection(self) -> ProjectionMatrix:
        """Initial W_d from domain expertise."""
        ...

    @property
    def projection(self) -> ProjectionMatrix:
        if self._projection is None:
            self._projection = self.default_projection()
        return self._projection

    @projection.setter
    def projection(self, w: ProjectionMatrix) -> None:
        self._projection = w

    def embed(self, **inputs) -> tuple[DomainMatrix, dict[str, float]]:
        """Full pipeline: physics → matrix → projection → Φ dict."""
        matrix = self.compute_matrix(**inputs)
        phi = self.projection.project(matrix)
        return matrix, phi

    @abstractmethod
    def get_params(self) -> dict[str, float]: ...
    @abstractmethod
    def set_params(self, params: dict[str, float]) -> None: ...


@dataclass
class ObjectiveResult:
    """Universal F-OS objective: F(t) = α·N(t) + β·I(t)."""
    score: float
    negentropy: float
    info_gain: float
    alpha: float = 0.7
    beta: float = 0.3
    per_sub: dict[str, float] = field(default_factory=dict)
    gradient: dict[str, list[float]] = field(default_factory=dict)
    meta_gradient: dict[str, float] = field(default_factory=dict)
    domain_matrices: dict[str, DomainMatrix] = field(default_factory=dict)


@dataclass
class InvariantResult:
    id: str; name: str; passed: bool; detail: str = ""


class InvariantRegistry:
    """Universal invariant registry. Each F-OS instantiation registers its own."""
    def __init__(self):
        self._results: list[InvariantResult] = []
    def record(self, r: InvariantResult) -> None: self._results.append(r)
    @property
    def all_passing(self) -> bool:
        return bool(self._results) and all(r.passed for r in self._results)
    @property
    def summary(self) -> str:
        return "\n".join(f"{'✅' if r.passed else '❌'} {r.id}: {r.name}" for r in self._results)


@dataclass
class RatchetExperiment:
    """One experiment in the autoresearch loop. Universal."""
    id: str = field(default_factory=lambda: f"exp-{uuid.uuid4().hex[:8]}")
    level: int = 0  # 1=params, 2=embedding, 3=composition
    domain: str = ""
    baseline: float = 0.0
    candidate_val: float = 0.0
    delta: float = 0.0
    status: str = "pending"
    candidate_data: dict = field(default_factory=dict)
    description: str = ""
    def resolve(self, val: float) -> None:
        self.candidate_val = val
        self.delta = self.baseline - val
        self.status = "keep" if self.delta > 0.001 else "discard"
