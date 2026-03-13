"""
Stage 12: Self-Improvement Audit — continuous invariant verification.

v6: "Per-sub-score invariant verification" as a continuous production service.
This is NOT the CI invariant check. This runs CONTINUOUSLY in production,
verifying that the system is actually self-improving.

It checks two things every cycle:
  1. Are all 8 invariants passing? (structural health)
  2. Is I(t) positive? (is the system still learning?)

If I(t) turns negative for >2 weeks, the audit triggers an alert:
the system has stopped learning. This is the meta-meta-check.

Laws: V (this IS the law of self-improvement, enforced continuously)
"""
from __future__ import annotations
from dataclasses import dataclass
from src.core.types import SubScore
from src.core import invariants
from src.synthesize.objective import InformationGainRate

@dataclass
class AuditResult:
    invariants_passing: int
    invariants_total: int = 8
    info_gain_positive: bool = True
    stagnation_weeks: int = 0
    alert: str = ""

    @property
    def healthy(self) -> bool:
        return self.invariants_passing == self.invariants_total and self.info_gain_positive

async def run_audit(
    user_id: str, twin, synth_result,
    info_gain: InformationGainRate,
    stagnation_counter: int = 0,
) -> AuditResult:
    """Run once per feedback cycle. Continuous production service."""
    inv_results = await invariants.run_all(user_id, twin, synth_result)
    passing = sum(1 for r in inv_results if r.passed)
    
    i_positive = info_gain.total > 0.5  # Threshold: >0.5% learning rate
    weeks_stagnant = stagnation_counter + 1 if not i_positive else 0
    
    alert = ""
    if weeks_stagnant >= 2:
        alert = f"STAGNATION ALERT: I(t) negative for {weeks_stagnant} weeks. System has stopped learning."
    if passing < 8:
        failed = [r for r in inv_results if not r.passed]
        alert += f" INVARIANT ALERT: {8-passing} invariants failing: {[r.id for r in failed]}"
    
    return AuditResult(
        invariants_passing=passing, info_gain_positive=i_positive,
        stagnation_weeks=weeks_stagnant, alert=alert,
    )
