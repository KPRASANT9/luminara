"""
Stage 1: Capture — Oura Ring adapter.
Input: Oura Cloud API v2 (HTTP).
Output: CloudEvents on NATS JetStream.

Law I: raw_payload preserved. Nothing computed here.
Invariant I1: every reading becomes an immutable event.
Resilience: Circuit Breaker (Pattern 1).
"""
from __future__ import annotations
import asyncio
from typing import AsyncIterator, Optional
from src.core.types import CloudEvent, SubScore


class CircuitBreaker:
    """Pattern 1: 3 failures → 5 min backoff. Allostatic load shedding."""
    def __init__(self, threshold: int = 3, cooldown_s: int = 300):
        self._fails = 0
        self._threshold = threshold
        self._cooldown = cooldown_s
        self._open_until: Optional[float] = None

    def record_failure(self) -> None:
        self._fails += 1
        if self._fails >= self._threshold:
            import time; self._open_until = time.time() + self._cooldown

    def record_success(self) -> None:
        self._fails = 0; self._open_until = None

    @property
    def is_open(self) -> bool:
        if not self._open_until: return False
        import time
        if time.time() > self._open_until:
            self._open_until = None; self._fails = 0; return False
        return True


class OuraCapture:
    """
    Captures IBI, sleep, SpO2, temperature from Oura API.
    Emits CloudEvents. Never computes, never transforms, never filters.
    """
    def __init__(self, poll_interval_s: int = 60):
        self._interval = poll_interval_s
        self._breaker = CircuitBreaker()

    async def _poll(self, user_id: str) -> list[CloudEvent]:
        if self._breaker.is_open:
            return []  # Pattern 2: graceful degradation
        # Production: HTTP → parse → CloudEvent per reading
        return []

    async def stream(self, user_id: str) -> AsyncIterator[CloudEvent]:
        """Continuous capture. Never stops (autoresearch principle)."""
        while True:
            for event in await self._poll(user_id):
                yield event
            await asyncio.sleep(self._interval)
