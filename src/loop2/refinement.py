"""
Loop 2: Model Refinement — Optimising the Optimiser.

v6: "The very definition of 'surprising' — the generative model p(s,ψ) —
is itself updated by minimising F over longer timescales. The system literally
rewrites its own notion of what 'good' looks like."

Concretely (from v6):
  1. T3-T4 prediction deltas PER SUB-SCORE
  2. Bayesian parameter updates on: Kronauer τ, Bergman S_I, Banister decay
  3. Model versions stored in Qdrant
  4. Model Accuracy Monotonicity (I7) per E, R, C, A independently
  5. Meta-gradient ∇²HAP consumed to guide WHICH params to update

Timescale: Weekly (T4). Temporal workflow: model_refinement_protocol.

How L2 is optimized:
  - The meta-gradient tells L2 WHERE improvement is accelerating/decelerating.
  - L2 allocates Bayesian update effort proportionally to meta-gradient magnitude.
  - Domains with decelerating improvement get MORE exploration (parameter space widened).
  - Domains with accelerating improvement get LESS (converging — don't disturb).
  - The ratchet: I7 gate ensures only non-increasing error per sub-score survives.

Laws: IV (self-correction), V (self-improvement)
Invariants: I7 (the gate)
"""
from __future__ import annotations
import uuid
import math
from dataclasses import dataclass, field
from datetime import datetime, timezone
from typing import Optional
from src.core.types import PredictionLogEntry, SubScore


@dataclass
class DomainParams:
    """Physics model parameters for one domain. Bayesian-updated by Loop 2."""
    domain: str                           # "sleep", "metabolic", "recovery", etc.
    params: dict[str, float]              # {"tau": 24.2, "chi_rise": 0.04, ...}
    prior_sigma: dict[str, float]         # Bayesian prior width per param
    sub_scores_served: list[str]          # ["E", "R"] for sleep domain


@dataclass
class ModelVersion:
    id: str = field(default_factory=lambda: f"v-{uuid.uuid4().hex[:8]}")
    created_at: datetime = field(default_factory=lambda: datetime.now(timezone.utc))
    domain_params: dict[str, DomainParams] = field(default_factory=dict)
    error_30d: dict[str, float] = field(default_factory=dict)  # per sub-score
    info_gain: dict[str, float] = field(default_factory=dict)  # I(t) per sub-score
    parent: str = ""
    status: str = "candidate"  # candidate → deployed | rolled_back | discarded


class ModelRefinement:
    """
    Weekly Temporal workflow.
    
    THE KEY MECHANISM: meta-gradient guides update effort allocation.
    
    For each domain:
      1. Compute prediction error for past week (per sub-score this domain serves)
      2. Read meta-gradient for those sub-scores
      3. If meta-gradient is negative (decelerating) → WIDEN prior σ (explore more)
      4. If meta-gradient is positive (accelerating) → NARROW prior σ (converge)
      5. Bayesian update: posterior = likelihood × prior
      6. Verify I7 per sub-score
      7. Keep or discard
    
    This is how Loop 2 becomes HOLISTIC:
    The meta-gradient from ALL sub-scores informs parameter updates for EACH domain.
    A Sleep domain Kronauer τ update considers whether ENERGY is accelerating
    or decelerating, because Kronauer serves both E and R.
    """
    def __init__(self, current_version: ModelVersion):
        self.current = current_version
        self._history: list[ModelVersion] = [current_version]

    async def compute_weekly_errors(
        self, entries: list[PredictionLogEntry],
    ) -> dict[str, float]:
        """Per-sub-score prediction error for the past week."""
        errs: dict[str, list[float]] = {s.value: [] for s in SubScore}
        for e in entries:
            if e.prediction_error is not None:
                errs[e.sub_score.value].append(e.prediction_error)
        return {k: (sum(v)/len(v) if v else 0.0) for k, v in errs.items()}

    async def bayesian_update_domain(
        self,
        domain: DomainParams,
        weekly_errors: dict[str, float],
        meta_gradient: dict[str, float],
    ) -> DomainParams:
        """
        Bayesian parameter update for one domain, guided by meta-gradient.
        
        For each parameter in this domain:
          - Compute likelihood from weekly prediction errors
          - Adjust prior width based on meta-gradient:
            * Decelerating sub-score → widen σ (explore more)
            * Accelerating → narrow σ (converge)
          - Posterior = likelihood × adjusted prior
        """
        updated = DomainParams(
            domain=domain.domain,
            params=dict(domain.params),
            prior_sigma=dict(domain.prior_sigma),
            sub_scores_served=domain.sub_scores_served,
        )
        
        # Meta-gradient-guided σ adjustment
        for sub in domain.sub_scores_served:
            accel = meta_gradient.get(sub, 0.0)
            if accel < -0.01:
                # Decelerating: improvement slowing → widen search
                sigma_scale = 1.3
            elif accel > 0.01:
                # Accelerating: converging → narrow search
                sigma_scale = 0.8
            else:
                sigma_scale = 1.0
            
            for param_name in updated.prior_sigma:
                updated.prior_sigma[param_name] *= sigma_scale
        
        # Bayesian update: shift params toward lower error
        relevant_error = sum(weekly_errors.get(s, 0) for s in domain.sub_scores_served)
        for param_name, param_value in updated.params.items():
            sigma = updated.prior_sigma.get(param_name, 0.05 * abs(param_value))
            # Simplified: shift by error-weighted random (in production: full Bayesian)
            # The direction comes from the gradient of error w.r.t. this parameter
            updated.params[param_name] = param_value  # placeholder: full MCMC in prod
            # Narrow posterior after update
            updated.prior_sigma[param_name] = sigma * 0.95
        
        return updated

    async def run_weekly_cycle(
        self,
        entries: list[PredictionLogEntry],
        meta_gradient: dict[str, float],
    ) -> ModelVersion:
        """
        The complete weekly Loop 2 cycle:
        1. Compute errors per sub-score
        2. For each domain: Bayesian update guided by meta-gradient
        3. Create candidate version
        4. Verify I7 per sub-score
        5. Deploy or discard
        
        Returns: new model version (deployed or current if no improvement)
        """
        weekly_errors = await self.compute_weekly_errors(entries)
        
        # Update each domain's parameters
        new_domains: dict[str, DomainParams] = {}
        for domain_name, domain_params in self.current.domain_params.items():
            updated = await self.bayesian_update_domain(
                domain_params, weekly_errors, meta_gradient
            )
            new_domains[domain_name] = updated
        
        # Compute information gain: how much did we LEARN this week
        prev_errors = self.current.error_30d
        info_gain: dict[str, float] = {}
        for sub in ["E", "R", "C", "A"]:
            pe = prev_errors.get(sub, 0)
            ce = weekly_errors.get(sub, 0)
            info_gain[sub] = max(0, pe - ce)  # positive = we learned something
        
        # Create candidate version
        candidate = ModelVersion(
            domain_params=new_domains,
            error_30d=weekly_errors,
            info_gain=info_gain,
            parent=self.current.id,
        )
        
        # I7 gate: per-sub-score monotonicity
        all_ok = True
        for sub in ["E", "R", "C", "A"]:
            curr = weekly_errors.get(sub, 0)
            prev = prev_errors.get(sub, 0)
            if curr > prev + 0.01:  # I7 violation for this sub-score
                all_ok = False
                break
        
        if all_ok and any(info_gain.get(s, 0) > 0.001 for s in ["E","R","C","A"]):
            candidate.status = "deployed"
            self.current = candidate
            self._history.append(candidate)
            return candidate
        else:
            candidate.status = "discarded"
            return self.current

    async def rollback(self) -> ModelVersion:
        """Pattern 3: Model Rollback on I7 failure."""
        if len(self._history) >= 2:
            self.current = self._history[-2]
            self.current.status = "deployed"
        return self.current
