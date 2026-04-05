"""
Core Types — The substrate of the entire HAPOS lifecycle.
Not a pipeline stage. The types that every stage depends on.

Every type answers three questions:
  1. Which sub-score(s) does it serve? (E/R/C/A)
  2. Which law(s) govern it? (I-V)
  3. Which invariant(s) must it preserve? (I1-I8)
"""
from __future__ import annotations
import uuid
from dataclasses import dataclass, field
from datetime import datetime, timezone
from enum import Enum
from typing import Any, Optional


class SubScore(str, Enum):
    ENERGY = "E"
    RECOVERY = "R"
    COGNITIVE = "C"
    AGENCY = "A"


@dataclass(frozen=True)
class CloudEvent:
    """
    Immutable sensor event. The atom of truth.
    Law I: Information cannot be destroyed after construction.
    Invariant I1: This event must be recoverable by id forever.
    """
    id: str = field(default_factory=lambda: str(uuid.uuid4()))
    source: str = ""            # "hapos://capture/oura/v2"
    type: str = ""              # "bio.cardiac.ibi"
    subject: str = ""           # "user:u_abc123"
    time: datetime = field(default_factory=lambda: datetime.now(timezone.utc))
    value: float | dict[str, Any] = 0.0
    unit: str = ""
    sensor_id: str = ""
    confidence: float = 1.0
    sub_score_targets: tuple[SubScore, ...] = ()
    raw_payload: Optional[dict[str, Any]] = None  # Law I: original vendor data preserved

    def __post_init__(self):
        assert self.type, "type required"
        assert self.subject, "subject required"
        assert len(self.sub_score_targets) > 0, "sub_score_targets required"
        assert 0.0 <= self.confidence <= 1.0


@dataclass
class PhiPartition:
    """
    One sub-score's Φ components. Written by embed stage. Read by synthesize stage.
    Law II: Partitions are independent. Composed only at synthesis.
    """
    sub_score: SubScore
    components: dict[str, float] = field(default_factory=dict)
    confidence: dict[str, float] = field(default_factory=dict)
    updated_at: datetime = field(default_factory=lambda: datetime.now(timezone.utc))


@dataclass
class TwinState:
    """
    S(t) = [Φ, Σ, Δ, Κ, Π] — the digital twin.
    Single source of truth stored in NATS KV.
    Written by: embed (Φ), couple (Κ), loop3 (Π).
    Read by: synthesize, feedback, loop2.
    """
    user_id: str
    phi_e: PhiPartition = field(default_factory=lambda: PhiPartition(SubScore.ENERGY))
    phi_r: PhiPartition = field(default_factory=lambda: PhiPartition(SubScore.RECOVERY))
    phi_c: PhiPartition = field(default_factory=lambda: PhiPartition(SubScore.COGNITIVE))
    phi_a: PhiPartition = field(default_factory=lambda: PhiPartition(SubScore.AGENCY))
    kappa: dict[str, float] = field(default_factory=dict)   # 7 coupling pairs
    pi: dict[str, float] = field(default_factory=dict)      # precision vector
    hap_score: float = 50.0
    model_version: str = "v0.0.0"
    updated_at: datetime = field(default_factory=lambda: datetime.now(timezone.utc))

    def get_phi(self, sub: SubScore) -> PhiPartition:
        return {"E": self.phi_e, "R": self.phi_r,
                "C": self.phi_c, "A": self.phi_a}[sub.value]

    async def snapshot(self) -> dict:
        """Nightly snapshot for I5 (Temporal Invertibility)."""
        from dataclasses import asdict
        return asdict(self)


@dataclass
class PredictionLogEntry:
    """
    Every prediction the system makes. Written by synthesize. Closed by feedback.
    Law IV: feeds loop2. Law V: feeds I7.
    Autoresearch fields: ratchet experiment tracking.
    """
    id: str = field(default_factory=lambda: str(uuid.uuid4()))
    ts: datetime = field(default_factory=lambda: datetime.now(timezone.utc))
    user_id: str = ""
    sub_score: SubScore = SubScore.ENERGY
    domain: str = ""
    predicted_value: float = 0.0
    actual_value: Optional[float] = None
    prediction_error: Optional[float] = None
    model_version: str = ""
    confidence_interval: tuple[float, float] = (0.0, 0.0)
    # Autoresearch ratchet fields
    experiment_id: str = ""
    ratchet_level: int = 0          # 1=sim, 2=model, 3=precision
    candidate_params: dict = field(default_factory=dict)
    baseline_error: float = 0.0
    candidate_error: float = 0.0
    status: str = "pending"         # pending → keep | discard | crash

    def close(self, actual: float) -> None:
        """Feedback stage closes the loop. Law IV."""
        self.actual_value = actual
        self.prediction_error = abs(self.predicted_value - actual)
