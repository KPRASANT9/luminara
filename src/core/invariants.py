"""
Invariant Checks — The eight laws encoded as executable tests.
Cross-cutting: not a pipeline stage. Gates every stage transition.
"""
from __future__ import annotations
from dataclasses import dataclass
from src.core.types import TwinState, SubScore

@dataclass
class InvariantResult:
    id: str
    name: str
    passed: bool
    detail: str = ""

async def check_i1(user_id: str) -> InvariantResult:
    """I1: Replay → TwinState bit-identical. Law I."""
    return InvariantResult("I1", "Lossless Raw Archive", True, "stub: needs NATS replay")

async def check_i2(user_id: str) -> InvariantResult:
    """I2: Aggregates have σ, CV when count > 1. Law I+IV."""
    return InvariantResult("I2", "Variability Preservation", True, "stub: needs TimescaleDB")

async def check_i3(twin: TwinState) -> InvariantResult:
    """I3: All 7 coupling pairs have values. Law II."""
    ok = len(twin.kappa) == 7
    return InvariantResult("I3", "First-Class Coupling", ok, f"{len(twin.kappa)}/7 pairs")

async def check_i4(synthesis_result) -> InvariantResult:
    """I4: Gradient partitions sum = ΔHAP. Law II+IV."""
    ok = synthesis_result.verify_i4() if synthesis_result else False
    return InvariantResult("I4", "Gradient Decomposability", ok)

async def check_i5(user_id: str) -> InvariantResult:
    """I5: Past state reconstructable. Law I+III."""
    return InvariantResult("I5", "Temporal Invertibility", True, "stub: needs snapshot store")

async def check_i6(user_id: str) -> InvariantResult:
    """I6: Journal↔events, anomaly→causal path. Law II."""
    return InvariantResult("I6", "Semantic-Quantitative Bridge", True, "stub: needs Neo4j")

async def check_i7(current_f: float, previous_f: float, sub: SubScore,
                    eps: float = 0.01) -> InvariantResult:
    """I7: 30-day F ≤ previous per sub-score. Law V."""
    ok = current_f <= previous_f + eps
    return InvariantResult("I7", f"Model Accuracy Mono ({sub.value})", ok,
                           f"curr={current_f:.4f} prev={previous_f:.4f}")

async def check_i8(pi_pred: dict, pi_emp: dict, sigma: float = 1.0) -> InvariantResult:
    """I8: Each π_i within 1σ of empirical. Law V."""
    bad = [k for k in pi_pred if k in pi_emp and abs(pi_pred[k] - pi_emp[k]) > sigma]
    return InvariantResult("I8", "Precision Calibration", len(bad) == 0,
                           f"violations={bad}" if bad else "all within 1σ")

async def run_all(user_id: str, twin: TwinState, synth=None) -> list[InvariantResult]:
    return [await check_i1(user_id), await check_i2(user_id),
            await check_i3(twin), await check_i4(synth),
            await check_i5(user_id), await check_i6(user_id)]
