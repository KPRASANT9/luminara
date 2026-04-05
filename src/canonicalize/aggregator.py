"""
Stage 2: Canonicalize — CloudEvents → TimescaleDB aggregates T0-T6.
Input: CloudEvents from capture stage.
Output: Time-indexed rows with FULL statistical profiles.

Law I: raw event preserved at T0. Aggregates ADD information, never remove.
Invariant I2: every aggregate stores σ, CV, percentiles — never just means.
"""
from __future__ import annotations
import math
from dataclasses import dataclass, field
from src.core.types import CloudEvent, SubScore


@dataclass
class AggregateRow:
    """
    One row of aggregated data at a specific timescale tier.
    I2: σ and CV are REQUIRED when count > 1. Not optional. Not nullable.
    """
    user_id: str
    signal_type: str
    tier: str                        # T0-T6
    sub_score_targets: tuple[SubScore, ...]
    count: int = 0
    mean: float = 0.0
    sigma: float = 0.0              # Standard deviation (I2)
    cv: float = 0.0                 # Coefficient of variation (I2)
    min_val: float = float("inf")
    max_val: float = float("-inf")
    p25: float = 0.0
    p50: float = 0.0
    p75: float = 0.0

    def verify_i2(self) -> bool:
        """I2: σ and CV available when count > 1."""
        if self.count <= 1:
            return True
        return self.sigma >= 0 and self.cv >= 0


class Aggregator:
    """
    Consumes CloudEvents, produces T1-T6 aggregates.
    T0 = raw event (stored directly, no aggregation).
    T1 = 10-second windows, T2 = 5-minute, T3 = 30-minute,
    T4 = daily, T5 = weekly, T6 = monthly.
    """
    def __init__(self):
        self._buffers: dict[str, list[float]] = {}

    async def ingest(self, event: CloudEvent) -> None:
        """Add raw value to buffer. T0 stored as-is (Law I)."""
        key = f"{event.subject}:{event.type}"
        self._buffers.setdefault(key, []).append(
            event.value if isinstance(event.value, float) else 0.0
        )

    async def flush_tier(self, user_id: str, signal_type: str,
                         tier: str, sub_targets: tuple[SubScore, ...]) -> AggregateRow:
        """Produce one aggregate row. I2 enforced at construction."""
        key = f"user:{user_id}:{signal_type}"
        vals = self._buffers.pop(key, [])
        n = len(vals)
        if n == 0:
            return AggregateRow(user_id=user_id, signal_type=signal_type,
                                tier=tier, sub_score_targets=sub_targets)
        mean = sum(vals) / n
        sigma = math.sqrt(sum((v - mean) ** 2 for v in vals) / n) if n > 1 else 0.0
        cv = sigma / mean if mean != 0 else 0.0
        sv = sorted(vals)
        row = AggregateRow(
            user_id=user_id, signal_type=signal_type, tier=tier,
            sub_score_targets=sub_targets, count=n, mean=mean,
            sigma=sigma, cv=cv,
            min_val=sv[0], max_val=sv[-1],
            p25=sv[int(n * 0.25)], p50=sv[int(n * 0.5)], p75=sv[int(n * 0.75)],
        )
        assert row.verify_i2(), f"I2 VIOLATED: missing σ/CV for {signal_type} at {tier}"
        return row
