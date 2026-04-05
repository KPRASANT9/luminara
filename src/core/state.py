"""
State Manager — Reads/writes TwinState to NATS KV.
Cross-cutting: serves all lifecycle stages.
"""
from __future__ import annotations
from datetime import datetime, timezone
from src.core.types import TwinState, PhiPartition, SubScore

class StateManager:
    """
    Partitioned reads/writes. Each lifecycle stage updates only its own outputs.
    - capture: no TwinState writes (events only)
    - canonicalize: no TwinState writes (aggregates only)
    - embed: writes Φ partitions
    - couple: writes Κ
    - synthesize: writes HAP score + gradient
    - loop3: writes Π
    """
    def __init__(self):
        self._states: dict[str, TwinState] = {}

    async def get(self, user_id: str) -> TwinState:
        if user_id not in self._states:
            self._states[user_id] = TwinState(user_id=user_id)
        return self._states[user_id]

    async def write_phi(self, user_id: str, sub: SubScore,
                        components: dict[str, float]) -> None:
        """Called by embed stage only. Law II: compositional isolation."""
        state = await self.get(user_id)
        phi = state.get_phi(sub)
        phi.components.update(components)
        phi.updated_at = datetime.now(timezone.utc)

    async def write_kappa(self, user_id: str, kappa: dict[str, float]) -> None:
        """Called by couple stage only. I3: all 7 pairs required."""
        state = await self.get(user_id)
        state.kappa.update(kappa)

    async def write_pi(self, user_id: str, pi: dict[str, float]) -> None:
        """Called by loop3 only."""
        state = await self.get(user_id)
        state.pi.update(pi)
