"""
Loop 3: Precision Controller — Optimising the Optimisation Process.

v6: "Higher layers learn to adjust:
  1. Precision (trust): Κ promoted from diagnostic to active control signal
  2. Policy space structure: prune MC branches with negligible ΔHAP
  3. Timescale and horizon depth: acute → T1-T3, stable → T5-T6
  The objective function is no longer fixed — it evolves."

How L3 is optimized:
  L3 consumes TWO signals:
    a) Loop 2 history: which domains improved most (information gain per domain)
    b) Meta-gradient ∇²HAP: which sub-scores are accelerating/decelerating
  
  From these, L3 adjusts THREE control surfaces:
  
  SURFACE 1: PRECISION (trust)
    When coupling weakens for a user, reduce precision weight for that pair.
    This is hierarchical precision: higher layers modulating lower-layer confidence.
    Wired to: synthesize/hap.py coupling_factor computation.
    
  SURFACE 2: POLICY SPACE STRUCTURE  
    If MC simulator keeps finding negligible ΔHAP for a domain, prune that branch.
    Reallocate rollout budget to high-information-gain domains.
    This is active inference: choosing policies that reduce model uncertainty.
    Wired to: autotune/simulator.py exploration_precision per domain.
    
  SURFACE 3: TIMESCALE / HORIZON DEPTH
    Acute stress (allostatic load rising) → short-horizon (T1-T3): focus on NOW.
    Stable maintenance (load decreasing) → long-horizon (T5-T6): plan ahead.
    Wired to: canonicalize/aggregator.py tier selection + synthesize/hap.py trajectory_adj.

How L3 feeds back holistically:
  The Π vector output by L3 flows to THREE consumers:
    1. synthesize/hap.py reads π weights for coupling_factor + sub-score weights
    2. autotune/simulator.py reads exploration_precision for candidate generation
    3. feedback/protocol.py reads horizon to adjust prediction window
  ALL consumers update on the same monthly cycle. No inconsistency.

Laws: IV (self-correction), V (self-improvement of the improvement process)
Invariants: I8 (precision calibration gate)
"""
from __future__ import annotations
import math
from dataclasses import dataclass, field
from typing import Optional
from src.core.types import SubScore
from src.loop2.refinement import ModelVersion


@dataclass
class PrecisionVector:
    """
    Π — the THREE control surfaces, unified in one structure.
    
    Read by:
      - synthesize/hap.py (sub_score_weights + coupling_trust)
      - autotune/simulator.py (exploration per domain)
      - feedback/protocol.py (active_horizon)
    """
    # SURFACE 1: Precision / trust
    sub_score_weights: dict[str, float] = field(default_factory=lambda: {
        "E": 0.30, "R": 0.30, "C": 0.20, "A": 0.20
    })
    coupling_trust: dict[str, float] = field(default_factory=lambda: {
        "E_E": 1.0, "E_R": 1.0, "R_R": 1.0, "R_E": 1.0,
        "C_A": 1.0, "E_C": 1.0, "ALL": 1.0,
    })
    
    # SURFACE 2: Policy space structure
    exploration_precision: dict[str, float] = field(default_factory=lambda: {
        "sleep": 1.0, "metabolic": 1.0, "recovery": 1.0,
        "cognitive": 1.0, "agency": 1.0,
    })
    policy_mask: dict[str, bool] = field(default_factory=lambda: {
        "sleep_timing": True, "exercise_timing": True,
        "meal_timing": True, "supplement": False,
        "cognitive_breaks": True, "goal_setting": True,
    })
    
    # SURFACE 3: Timescale / horizon depth
    active_horizon: str = "balanced"  # "short" (T1-T3) | "balanced" (T3-T5) | "long" (T5-T6)
    trajectory_sensitivity: float = 2.0  # How strongly Δ influences HAP synthesis
    prediction_window_days: int = 7      # How far ahead predictions extend
    
    def verify_weights_sum(self) -> bool:
        total = sum(self.sub_score_weights.values())
        return abs(total - 1.0) < 0.01

    @staticmethod
    def neutral() -> PrecisionVector:
        """Safe defaults. Pattern 4: bad meta-params worse than none."""
        return PrecisionVector()


class PrecisionController:
    """
    Monthly meta-learner. Adjusts all three surfaces based on Loop 2 history + meta-gradient.
    
    THE HOLISTIC UPDATE:
    All three surfaces are updated TOGETHER from the same two signals,
    ensuring consistency. You can't have high exploration precision in a
    domain that's been pruned from policy space. You can't have long-horizon
    planning while coupling trust is collapsing.
    """
    def __init__(self, pi: Optional[PrecisionVector] = None):
        self.pi = pi or PrecisionVector()
        self._history: list[PrecisionVector] = [self.pi]

    async def update(
        self,
        loop2_versions: list[ModelVersion],
        meta_gradient: dict[str, float],
        coupling_history: list[dict[str, float]],
        allostatic_trend: float,       # positive = worsening, negative = improving
        empirical_coupling: dict[str, float],
    ) -> PrecisionVector:
        """
        The complete monthly Loop 3 cycle.
        All three surfaces updated from the same data.
        
        Returns: new Π or neutral if I8 fails.
        """
        new = PrecisionVector(
            sub_score_weights=dict(self.pi.sub_score_weights),
            coupling_trust=dict(self.pi.coupling_trust),
            exploration_precision=dict(self.pi.exploration_precision),
            policy_mask=dict(self.pi.policy_mask),
            active_horizon=self.pi.active_horizon,
            trajectory_sensitivity=self.pi.trajectory_sensitivity,
            prediction_window_days=self.pi.prediction_window_days,
        )
        
        # ── SIGNAL 1: Information gain per domain from Loop 2 history ──
        domain_gain: dict[str, float] = {}
        for version in loop2_versions:
            for domain, dp in version.domain_params.items():
                for sub in dp.sub_scores_served:
                    key = domain
                    gain = version.info_gain.get(sub, 0.0)
                    domain_gain[key] = domain_gain.get(key, 0.0) + gain
        
        total_gain = sum(domain_gain.values()) or 1.0
        
        # ── SURFACE 1: Precision (trust) ──
        # Adjust coupling trust based on empirical drift
        if len(coupling_history) >= 2:
            recent = coupling_history[-1]
            older = coupling_history[0]
            for pair in new.coupling_trust:
                drift = abs(recent.get(pair, 0.5) - older.get(pair, 0.5))
                if drift > 0.15:
                    new.coupling_trust[pair] *= 0.85  # Reduce trust
                elif drift < 0.05:
                    new.coupling_trust[pair] = min(1.0, new.coupling_trust[pair] * 1.05)
        
        # Adjust sub-score weights based on meta-gradient
        for sub in ["E", "R", "C", "A"]:
            accel = meta_gradient.get(sub, 0.0)
            if accel > 0.01:
                # Accelerating: this sub-score is responding → slightly increase weight
                new.sub_score_weights[sub] = min(0.45, new.sub_score_weights[sub] + 0.02)
            elif accel < -0.01:
                # Decelerating: not responding → slightly decrease weight
                new.sub_score_weights[sub] = max(0.10, new.sub_score_weights[sub] - 0.02)
        # Re-normalize weights to sum to 1
        wtotal = sum(new.sub_score_weights.values())
        for k in new.sub_score_weights:
            new.sub_score_weights[k] /= wtotal
        
        # ── SURFACE 2: Policy space structure ──
        # Allocate exploration to high-gain domains; prune low-gain
        for domain in new.exploration_precision:
            domain_info = domain_gain.get(domain, 0.0) / total_gain
            if domain_info < 0.05:
                # Low gain → reduce exploration (prune)
                new.exploration_precision[domain] = max(0.2, new.exploration_precision[domain] * 0.7)
            else:
                # High gain → increase exploration
                new.exploration_precision[domain] = min(3.0, 0.5 + 2.5 * domain_info)
        
        # Prune policy actions for domains with sustained zero gain
        for domain in list(new.policy_mask.keys()):
            related_domain = domain.split("_")[0]  # "sleep_timing" → "sleep"
            if domain_gain.get(related_domain, 0) < 0.001 and len(loop2_versions) >= 4:
                new.policy_mask[domain] = False  # Prune: no info gain after 4 weeks
        
        # ── SURFACE 3: Timescale / horizon depth ──
        if allostatic_trend > 0.1:
            # Stress rising → short horizon: focus on immediate recovery
            new.active_horizon = "short"
            new.trajectory_sensitivity = 3.0   # More responsive to Δ
            new.prediction_window_days = 3     # Short-term predictions only
        elif allostatic_trend < -0.1:
            # Stress falling → long horizon: plan ahead
            new.active_horizon = "long"
            new.trajectory_sensitivity = 1.5
            new.prediction_window_days = 14
        else:
            new.active_horizon = "balanced"
            new.trajectory_sensitivity = 2.0
            new.prediction_window_days = 7
        
        # ── I8 GATE: Precision calibration ──
        violations = 0
        for pair in new.coupling_trust:
            if pair in empirical_coupling:
                if abs(new.coupling_trust[pair] - empirical_coupling[pair]) > 1.0:
                    violations += 1
        
        if violations > 0 or not new.verify_weights_sum():
            new = PrecisionVector.neutral()  # Pattern 4: reset to safe defaults
        
        self.pi = new
        self._history.append(new)
        return new
