"""
Stage 6: Feedback — close the prediction-outcome loop.
Input: predictions from synthesize + actual outcomes from next capture cycle.
Output: closed PredictionLogEntries ready for loop2 consumption.

Law IV: every prediction must meet reality. No open loops.
Law III: exactly-once closure (Temporal crash recovery).
Invariant I7: closed entries feed the monotonicity check.
"""
from __future__ import annotations
from dataclasses import dataclass, field
from datetime import datetime, timezone
from typing import Optional
from src.core.types import PredictionLogEntry, SubScore

@dataclass
class FeedbackCycleState:
    """Temporal workflow state. Survives crashes (Law III)."""
    user_id: str
    date: str
    recommendations_sent: list[str] = field(default_factory=list)
    self_report_received: bool = False
    predictions_closed: int = 0

class FeedbackProtocol:
    """
    Daily workflow:
    1. Synthesize stage emits predictions + recommendations
    2. Capture stage collects next-day outcomes
    3. Feedback closes each prediction (predicted vs actual)
    4. Closed entries written to prediction_log for loop2
    
    This is the daily heartbeat. It runs every day. It never stops.
    """
    def __init__(self, state: Optional[FeedbackCycleState] = None):
        self.state = state or FeedbackCycleState(user_id="", date="")
        self._log: list[PredictionLogEntry] = []

    async def record_prediction(self, entry: PredictionLogEntry) -> None:
        """Synthesize stage calls this when emitting a recommendation."""
        self._log.append(entry)
        self.state.recommendations_sent.append(entry.id)

    async def record_self_report(self, clarity: float, energy: float) -> None:
        """User provides morning self-report (calibration anchor)."""
        self.state.self_report_received = True

    async def close_prediction(self, entry_id: str, actual: float) -> Optional[PredictionLogEntry]:
        """
        Close one prediction. Law IV: prediction meets reality.
        Returns the closed entry for loop2 consumption.
        """
        for entry in self._log:
            if entry.id == entry_id and entry.actual_value is None:
                entry.close(actual)
                entry.status = "closed"
                self.state.predictions_closed += 1
                return entry
        return None

    async def get_closed_entries(self) -> list[PredictionLogEntry]:
        """All closed entries, ready for loop2."""
        return [e for e in self._log if e.actual_value is not None]

    @property
    def loop_is_closed(self) -> bool:
        """Law IV: all predictions for this cycle have actual values."""
        return all(e.actual_value is not None for e in self._log)
