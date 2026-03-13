"""
Unified Objective: HAP(t) = α·N(t) + β·I(t)

N(t) = negentropy production rate — how much structured order you are creating NOW.
       This is the Loop 1 output. Computed from the four sub-scores.

I(t) = information gain rate — how much you are LEARNING about your own physics.
       This is the Loop 2 output. Computed from prediction error reduction over time.

π* = argmin E_q[F(s,π)] = argmin [E_q[-log p(s|π)] - H[q]]
The system doesn't just optimize current state. It actively designs experiments
to learn how your body works, permanently improving all future predictions.

Laws: All (this IS the objective that all laws serve)
Invariants: I4 (gradient), I7 (monotonicity of I(t))
Matrix Cell: S1-L1 (N) + S1-L2 (I) → synthesized here
"""
from __future__ import annotations
import math
from dataclasses import dataclass, field
from typing import Optional
from src.core.types import SubScore, PredictionLogEntry


@dataclass
class NegentropyRate:
    """
    N(t) — how much structured work the system is producing right now.
    Derived from the four sub-scores. N is what Loop 1 maximizes.
    
    Physics: dS/dt < 0 locally. The system imports more negentropy
    than it dissipates entropy. The sub-scores are the four dimensions
    of this negentropy production.
    """
    total: float = 0.0                    # Composite N(t) ∈ [0, 100]
    per_sub_score: dict[str, float] = field(default_factory=dict)  # N_E, N_R, N_C, N_A
    
    @staticmethod
    def from_sub_scores(e: float, r: float, c: float, a: float,
                         weights: dict[str, float] | None = None) -> NegentropyRate:
        """
        N(t) = weighted combination of sub-scores.
        Each sub-score IS a negentropy measure at its physics layer:
          E = thermodynamic negentropy (can the body do work?)
          R = control-theoretic negentropy (can it return to order?)
          C = information-theoretic negentropy (is its model accurate?)
          A = policy negentropy (are its actions structured?)
        """
        w = weights or {"E": 0.30, "R": 0.30, "C": 0.20, "A": 0.20}
        per = {"E": e, "R": r, "C": c, "A": a}
        total = sum(w[k] * per[k] for k in per)
        return NegentropyRate(total=total, per_sub_score=per)


@dataclass
class InformationGainRate:
    """
    I(t) — how much the system is LEARNING about the user's physics.
    Derived from Loop 2 model improvement. I is what Loop 2 maximizes.
    
    Physics: The rate at which F (variational free energy) is decreasing
    due to model updates, not due to actions. This is pure epistemic gain:
    the world didn't change, our MODEL of it got better.
    """
    total: float = 0.0                    # Composite I(t) ∈ [0, 100]
    per_sub_score: dict[str, float] = field(default_factory=dict)  # I_E, I_R, I_C, I_A
    model_accuracy_trend: dict[str, float] = field(default_factory=dict)  # dF/dt per sub-score
    
    @staticmethod
    def from_prediction_history(
        recent_entries: list[PredictionLogEntry],
        previous_entries: list[PredictionLogEntry],
    ) -> InformationGainRate:
        """
        I(t) = reduction in prediction error between two time windows.
        Positive I = model is getting better. Negative I = model is regressing.
        
        For each sub-score: I_s = (error_previous - error_recent) / error_previous
        Normalized to [0, 100]: 0 = no learning, 100 = perfect learning.
        """
        def _mean_error(entries: list[PredictionLogEntry], sub: str) -> float:
            errs = [e.prediction_error for e in entries
                    if e.sub_score.value == sub and e.prediction_error is not None]
            return sum(errs) / len(errs) if errs else 1.0

        per: dict[str, float] = {}
        trends: dict[str, float] = {}
        for sub in ["E", "R", "C", "A"]:
            prev_err = _mean_error(previous_entries, sub)
            curr_err = _mean_error(recent_entries, sub)
            if prev_err > 0:
                gain = max(0, (prev_err - curr_err) / prev_err)
                per[sub] = gain * 100  # [0, 100]
                trends[sub] = curr_err - prev_err  # negative = improving
            else:
                per[sub] = 50.0  # neutral if no data
                trends[sub] = 0.0

        total = sum(per.values()) / len(per) if per else 0.0
        return InformationGainRate(total=total, per_sub_score=per,
                                    model_accuracy_trend=trends)


@dataclass
class MetaGradient:
    """
    ∇²HAP = d(∇HAP)/dt at T4/T5.
    The second derivative of HAP with respect to time.
    
    This tells Loop 2 and Loop 3 WHERE the gradient is accelerating
    or decelerating. It is the signal that drives meta-learning:
    - Positive ∇² for a sub-score → improvement is accelerating → keep doing this
    - Negative ∇² → improvement is decelerating → change strategy
    - Zero ∇² → plateau → explore new dimensions
    
    v6 Stage 5 Step 6: "Meta-gradient feeds Loop 2 + Loop 3"
    """
    d_gradient_e: list[float] = field(default_factory=list)  # d(∂HAP/∂Φ_E)/dt
    d_gradient_r: list[float] = field(default_factory=list)
    d_gradient_c: list[float] = field(default_factory=list)
    d_gradient_a: list[float] = field(default_factory=list)
    per_sub_score_acceleration: dict[str, float] = field(default_factory=dict)

    @staticmethod
    def from_gradient_history(
        gradient_t: dict[str, list[float]],
        gradient_t_minus_1: dict[str, list[float]],
        dt_days: float = 7.0,
    ) -> MetaGradient:
        """
        Compute ∇²HAP = (∇HAP(t) - ∇HAP(t-1)) / dt.
        Positive = gradient is increasing (accelerating improvement).
        """
        mg = MetaGradient()
        for sub in ["E", "R", "C", "A"]:
            curr = gradient_t.get(sub, [])
            prev = gradient_t_minus_1.get(sub, [])
            if curr and prev and len(curr) == len(prev):
                delta = [(c - p) / dt_days for c, p in zip(curr, prev)]
                setattr(mg, f"d_gradient_{sub.lower()}", delta)
                mg.per_sub_score_acceleration[sub] = sum(delta) / len(delta)
            else:
                mg.per_sub_score_acceleration[sub] = 0.0
        return mg


@dataclass
class UnifiedObjective:
    """
    The complete HAP(t) = α·N(t) + β·I(t) computation.
    
    N(t) = negentropy production rate (Loop 1 output: how good are you NOW)
    I(t) = information gain rate (Loop 2 output: how much is the system LEARNING)
    
    α and β are hyperparameters:
    - α = 0.7 by default (weight on current state)
    - β = 0.3 by default (weight on learning rate)
    
    This means HAPOS values a system that is both performing well AND learning fast.
    A person with HAP=80 and I(t)=0 (no learning) scores lower than a person with
    HAP=70 and I(t)=30 (rapidly improving model). The system incentivizes exploration.
    """
    hap: float                           # Final HAP = α·N + β·I
    negentropy: NegentropyRate           # N(t) from sub-scores
    info_gain: InformationGainRate       # I(t) from prediction history
    meta_gradient: MetaGradient          # ∇²HAP for Loop 2+3 consumption
    alpha: float = 0.7                   # Weight on current state
    beta: float = 0.3                    # Weight on learning rate

    @staticmethod
    def compute(
        energy: float, recovery: float, cognitive: float, agency: float,
        recent_preds: list[PredictionLogEntry],
        previous_preds: list[PredictionLogEntry],
        gradient_t: dict[str, list[float]],
        gradient_t_minus_1: dict[str, list[float]],
        alpha: float = 0.7,
        beta: float = 0.3,
    ) -> UnifiedObjective:
        """
        The master computation. This IS what HAPOS optimizes.
        
        π* = argmin E_q[F(s,π)]
        By computing both N and I, the system selects policies that
        reduce surprise (high N) AND design experiments that improve
        the model (high I). A system that gets better at getting better.
        """
        n = NegentropyRate.from_sub_scores(energy, recovery, cognitive, agency)
        i = InformationGainRate.from_prediction_history(recent_preds, previous_preds)
        mg = MetaGradient.from_gradient_history(gradient_t, gradient_t_minus_1)
        hap = alpha * n.total + beta * i.total
        return UnifiedObjective(hap=max(0, min(100, hap)),
                                negentropy=n, info_gain=i,
                                meta_gradient=mg, alpha=alpha, beta=beta)
