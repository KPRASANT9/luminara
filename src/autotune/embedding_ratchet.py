"""
Embedding Ratchet — optimizes projection matrices W_d per domain.

This is the innovation: a new optimization level between Loop 1 and Loop 2.
  Level 1 (Loop 2): tunes physics params → M_d content changes
  Level 2 (THIS): tunes W_d → what synthesize sees changes
  Level 3 (Loop 3): tunes Π → how Φ's compose into HAP

The Embedding Ratchet asks: given the SAME physics model outputs (M_d),
is there a better projection that would reduce prediction error?

This matters because the default W_d is set by domain expertise, but
the INDIVIDUAL may have different structure. Person A's circadian phase
might matter more than their kuramoto_r. Person B might be the opposite.
The ratchet discovers this without touching the physics.

Autoresearch protocol:
  1. Read current W_d (baseline)
  2. Perturb W_d (guided by Π exploration_precision)
  3. Re-project M_d through perturbed W_d → new Φ → new HAP
  4. Compare to baseline HAP prediction error
  5. Keep if error decreases; discard otherwise
  6. Log every experiment

Time budget: milliseconds (projection is pure matrix multiply).
Runs: after every Loop 1 cycle, before Loop 2.
Frequency: ~1000 candidates per daily cycle.
"""
from __future__ import annotations
import uuid
from dataclasses import dataclass, field
from src.core.fos import (
    DomainEmbedder, DomainMatrix, ProjectionMatrix, RatchetExperiment
)
from src.synthesize.hap import synthesize


@dataclass
class EmbeddingCandidate:
    id: str = field(default_factory=lambda: f"emb-{uuid.uuid4().hex[:6]}")
    domain: str = ""
    projection: ProjectionMatrix | None = None
    hap_delta: float = 0.0
    per_sub_delta: dict[str, float] = field(default_factory=dict)
    i4_valid: bool = True


class EmbeddingRatchet:
    """
    Optimizes W_d for each domain embedder.
    
    The key insight: physics params and embedding projections are
    INDEPENDENT optimization targets. Changing W_d doesn't change
    what the physics model computes — only what the synthesize stage
    sees. This means:
    
    1. We can run the Embedding Ratchet at machine speed (no physics recomputation)
    2. W_d optimization is per-individual (different people need different projections)
    3. The physics integrity is preserved (M_d never changes)
    4. I4 (gradient decomposability) is verified after every projection change
    
    The Embedding Ratchet runs DAILY (after embed, before synthesize).
    Loop 2 (params) runs WEEKLY.
    They don't interfere: W_d optimizes the projection, τ optimizes the content.
    """
    def __init__(self):
        self._experiments: list[RatchetExperiment] = []

    async def optimize_projection(
        self,
        embedder: DomainEmbedder,
        domain_matrix: DomainMatrix,
        all_phi: dict[str, dict[str, float]],
        kappa: dict[str, float],
        pi: dict[str, float],
        n_candidates: int = 100,
        top_n: int = 5,
    ) -> list[EmbeddingCandidate]:
        """
        Run Embedding Ratchet for one domain.
        
        Args:
            embedder: The domain embedder with current W_d
            domain_matrix: M_d from most recent physics computation
            all_phi: Current Φ components for ALL sub-scores (for HAP recomputation)
            kappa, pi: Current coupling and precision vectors
            
        Returns: Top candidates sorted by ΔHAP improvement.
        """
        # Baseline: current projection
        current_phi = embedder.projection.project(domain_matrix)
        baseline_phi = {**all_phi}
        # Merge this domain's Φ into the full set
        for target in embedder.targets:
            key = f"phi_{target.lower()}"
            if key not in baseline_phi:
                baseline_phi[target] = current_phi
            else:
                baseline_phi[target].update(current_phi)

        try:
            baseline_hap = synthesize(
                baseline_phi.get("E", {}), baseline_phi.get("R", {}),
                baseline_phi.get("C", {}), baseline_phi.get("A", {}),
                kappa, pi,
            )
            baseline_score = baseline_hap.hap
        except Exception:
            return []

        candidates: list[EmbeddingCandidate] = []

        for _ in range(n_candidates):
            # Perturb W_d
            perturbed_w = embedder.projection.perturb(scale=0.03)
            perturbed_phi = perturbed_w.project(domain_matrix)

            # Build modified full Φ
            test_phi = {k: dict(v) if isinstance(v, dict) else v for k, v in baseline_phi.items()}
            for target in embedder.targets:
                if target in test_phi and isinstance(test_phi[target], dict):
                    test_phi[target].update(perturbed_phi)

            try:
                test_hap = synthesize(
                    test_phi.get("E", {}), test_phi.get("R", {}),
                    test_phi.get("C", {}), test_phi.get("A", {}),
                    kappa, pi,
                )
                delta = test_hap.hap - baseline_score
                per_sub = {
                    "E": test_hap.energy.score - baseline_hap.energy.score,
                    "R": test_hap.recovery.score - baseline_hap.recovery.score,
                    "C": test_hap.cognitive.score - baseline_hap.cognitive.score,
                    "A": test_hap.agency.score - baseline_hap.agency.score,
                }
                cand = EmbeddingCandidate(
                    domain=embedder.domain, projection=perturbed_w,
                    hap_delta=delta, per_sub_delta=per_sub,
                    i4_valid=test_hap.verify_i4(),
                )
                if cand.i4_valid:
                    candidates.append(cand)

                    # Log experiment
                    exp = RatchetExperiment(
                        level=2, domain=embedder.domain,
                        baseline=baseline_score, candidate_val=test_hap.hap,
                        description=f"W_d perturbation for {embedder.domain}",
                    )
                    exp.resolve(test_hap.hap)
                    self._experiments.append(exp)

            except Exception:
                pass  # Simulation crashed — discard

        # Rank by positive ΔHAP
        candidates.sort(key=lambda c: c.hap_delta, reverse=True)
        return candidates[:top_n]

    async def apply_best(self, embedder: DomainEmbedder,
                          candidates: list[EmbeddingCandidate]) -> bool:
        """Apply the best candidate if it improves HAP. Ratchet: keep or discard."""
        if not candidates or candidates[0].hap_delta <= 0.001:
            return False
        best = candidates[0]
        if best.projection is not None:
            embedder.projection = best.projection
            return True
        return False

    @property
    def experiment_count(self) -> int:
        return len(self._experiments)

    @property
    def keep_rate(self) -> float:
        if not self._experiments:
            return 0.0
        kept = sum(1 for e in self._experiments if e.status == "keep")
        return kept / len(self._experiments)
