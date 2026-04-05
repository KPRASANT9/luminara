"""
Ratchet 1: Simulation AutoTuner — machine-speed parameter search.
Input: TwinState + base params + Π exploration weights.
Output: ranked candidate list for Ratchet 2 validation.

Autoresearch: 1000 candidates per cycle. Milliseconds each.
NEVER STOP: generates candidates continuously between loop2 cycles.
"""
from __future__ import annotations
import random
import uuid
from dataclasses import dataclass, field
from src.core.types import TwinState
from src.synthesize.hap import synthesize, HAPResult
from src.loop3.precision import PrecisionVector

@dataclass
class Candidate:
    id: str = field(default_factory=lambda: f"c-{uuid.uuid4().hex[:6]}")
    params: dict[str, float] = field(default_factory=dict)
    domain: str = ""
    delta_hap: float = 0.0
    i4_valid: bool = True

class Simulator:
    def __init__(self, pi: PrecisionVector | None = None):
        self.pi = pi or PrecisionVector()

    def _perturb(self, base: dict[str, float], domain: str) -> Candidate:
        scale = 0.05 / max(0.1, self.pi.exploration.get(domain, 1.0))
        perturbed = {k: v + random.gauss(0, scale * abs(v) if v else scale)
                     for k, v in base.items()}
        return Candidate(params=perturbed, domain=domain)

    async def run_cycle(self, twin: TwinState,
                        base_params: dict[str, dict[str, float]],
                        n: int = 1000, top: int = 10) -> list[Candidate]:
        baseline = synthesize(
            twin.phi_e.components, twin.phi_r.components,
            twin.phi_c.components, twin.phi_a.components,
            twin.kappa, twin.pi,
        )
        results: list[Candidate] = []
        domains = list(base_params.keys())
        for i in range(n):
            d = domains[i % len(domains)]
            c = self._perturb(base_params.get(d, {}), d)
            try:
                sim = synthesize(
                    twin.phi_e.components, twin.phi_r.components,
                    twin.phi_c.components, twin.phi_a.components,
                    twin.kappa, twin.pi,
                )
                c.delta_hap = sim.hap - baseline.hap
                c.i4_valid = sim.verify_i4()
                results.append(c)
            except Exception:
                pass
        valid = [r for r in results if r.i4_valid]
        valid.sort(key=lambda r: r.delta_hap, reverse=True)
        return valid[:top]
