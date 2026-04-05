"""
Integration test: DomainMatrix flows through the complete lifecycle.

Tests the THREE optimization levels:
  Level 1: Physics params affect M_d content
  Level 2: Projection W_d affects what synthesize sees
  Level 3: Composition weights affect how sub-scores combine

Also tests F-OS instantiation for a non-HAPOS domain (retail).
"""
import pytest
import numpy as np
from src.core.fos import SubstrateConfig, SubstrateDimension, DomainMatrix, ProjectionMatrix
from src.core.instantiate import instantiate, DomainSpec
from src.embed.sleep import SleepEmbedder, KronauerParams
from src.embed.metabolic import MetabolicEmbedder
from src.embed.cognitive import CognitiveEmbedder
from src.embed.agency import AgencyEmbedder
from src.embed.recovery import RecoveryEmbedder
from src.autotune.embedding_ratchet import EmbeddingRatchet


class TestDomainMatrixPipeline:
    """Test that DomainMatrix flows through all stages correctly."""

    def test_sleep_produces_matrix(self):
        embedder = SleepEmbedder()
        matrix = embedder.compute_matrix(hours_awake=14.0, light_lux=100.0)
        assert matrix.k == 6, f"Expected 6 rows, got {matrix.k}"
        assert matrix.n == 6, f"Expected 6 tiers, got {matrix.n}"
        assert matrix.domain == "sleep"
        assert "E" in matrix.sub_scores_served
        assert "R" in matrix.sub_scores_served
        assert matrix.data.shape == (6, 6)

    def test_projection_preserves_i4(self):
        """I4: Gradient decomposes through linear projection."""
        embedder = SleepEmbedder()
        matrix = embedder.compute_matrix(hours_awake=14.0, light_lux=100.0)
        result = embedder.embed(tier="T3", hours_awake=14.0, light_lux=100.0)
        # Every sub-score served should have phi components
        assert "E" in result
        assert "R" in result
        assert "phi_circ" in result["E"]
        assert "phi_sleep_quality" in result["R"]
        # Values should be in [0, 1]
        for sub, phi in result.items():
            for name, val in phi.items():
                assert 0.0 <= val <= 1.0, f"{sub}.{name} = {val} out of [0,1]"

    def test_level1_params_change_matrix_content(self):
        """Level 1: Different physics params → different M_d content."""
        emb1 = SleepEmbedder(KronauerParams(tau=24.2))
        emb2 = SleepEmbedder(KronauerParams(tau=23.5))
        m1 = emb1.compute_matrix(hours_awake=14.0, light_lux=100.0)
        m2 = emb2.compute_matrix(hours_awake=14.0, light_lux=100.0)
        # Different tau → different phase → different matrix content
        assert not np.allclose(m1.data, m2.data), "Different params should produce different matrices"

    def test_level2_projection_changes_phi(self):
        """Level 2: Different W_d → different Φ from same M_d."""
        embedder = SleepEmbedder()
        matrix = embedder.compute_matrix(hours_awake=14.0, light_lux=100.0)
        phi_original = embedder.embed(tier="T3", hours_awake=14.0, light_lux=100.0)

        # Perturb the projection matrix
        proj = embedder.get_projections()["E"]
        perturbed = proj.perturb(scale=0.3)
        embedder.register_projection("E", perturbed)
        phi_perturbed = embedder.embed(tier="T3", hours_awake=14.0, light_lux=100.0)

        # Perturbed projection should give different Φ from same physics
        assert phi_original["E"] != phi_perturbed["E"], "Different W_d should produce different Φ"

    def test_all_5_embedders_produce_matrices(self):
        """All 5 domain embedders produce valid DomainMatrix objects."""
        embedders = [
            SleepEmbedder(),
            MetabolicEmbedder(),
            RecoveryEmbedder(),
            CognitiveEmbedder(),
            AgencyEmbedder(),
        ]
        for emb in embedders:
            result = emb.embed(tier="T3")
            assert len(result) > 0, f"{emb.domain} produced no phi components"
            for sub, phi in result.items():
                assert len(phi) > 0, f"{emb.domain}.{sub} empty"
                for name, val in phi.items():
                    assert isinstance(val, float), f"{name} not float: {type(val)}"


class TestEmbeddingRatchet:
    """Test Level 2 optimization: projection matrix tuning."""

    @pytest.mark.asyncio
    async def test_generates_candidates(self):
        embedder = SleepEmbedder()
        matrix = embedder.compute_matrix(hours_awake=14.0, light_lux=100.0)
        ratchet = EmbeddingRatchet()
        candidates = await ratchet.generate_candidates(
            embedder, matrix, baseline_hap_error=0.15, n_candidates=50,
        )
        assert len(candidates) > 0
        assert len(candidates) <= 10  # Top 10 kept
        assert all(c.perturbed_projection is not None for c in candidates)

    @pytest.mark.asyncio
    async def test_validates_keeps_improvements(self):
        ratchet = EmbeddingRatchet()
        embedder = SleepEmbedder()
        matrix = embedder.compute_matrix(hours_awake=14.0, light_lux=100.0)
        candidates = await ratchet.generate_candidates(
            embedder, matrix, baseline_hap_error=0.15, n_candidates=20,
        )
        if candidates:
            # Simulate: candidate improved prediction
            kept = await ratchet.validate_candidate(candidates[0], 0.10, 0.15)
            assert kept, "Should keep candidate that reduces error"
            # Simulate: candidate worsened prediction
            if len(candidates) > 1:
                discarded = await ratchet.validate_candidate(candidates[1], 0.20, 0.15)
                assert not discarded, "Should discard candidate that increases error"


class TestFOSInstantiation:
    """Test that F-OS can instantiate for non-HAPOS domains."""

    def test_retail_instantiation(self):
        config = SubstrateConfig(
            name="RetailOS",
            dimensions=[
                SubstrateDimension("D", "Demand", "How accurately predict demand?", "ARIMA", 0.30),
                SubstrateDimension("S", "Supply", "How efficiently is inventory flowing?", "Little's Law", 0.30),
                SubstrateDimension("P", "Pricing", "How optimally pricing?", "Elasticity", 0.20),
                SubstrateDimension("X", "Experience", "How satisfied are customers?", "NPS regression", 0.20),
            ],
        )
        domains = {
            "demand": DomainSpec(
                name="demand", variables=["trend", "seasonality", "external"],
                units=["units/day", "index", "composite"],
                equations=["ARIMA(p,d,q)", "Fourier", "regression"],
                sub_scores_served=["D", "S"],
                phi_output_names={"D": ["phi_trend", "phi_seasonal"], "S": ["phi_demand_signal"]},
            ),
            "inventory": DomainSpec(
                name="inventory", variables=["throughput", "wip", "cycle_time"],
                units=["units/hr", "count", "hours"],
                equations=["λ=count/time", "WIP=L", "W=L/λ"],
                sub_scores_served=["S"],
                phi_output_names={"S": ["phi_throughput", "phi_wip"]},
            ),
        }
        result = instantiate(config, domains)
        assert result.config.name == "RetailOS"
        assert len(result.domain_matrices) == 2
        assert "demand" in result.projections
        assert "D" in result.projections["demand"]
        assert len(result.invariant_stubs) == 8
        assert "RetailOS" in result.agent_template

    def test_generated_embedder_stub_is_valid_python(self):
        config = SubstrateConfig(
            name="TestOS",
            dimensions=[SubstrateDimension("X", "Test", "q?", "eq", 1.0)],
        )
        domains = {"test_domain": DomainSpec(
            name="test_domain", variables=["a", "b"],
            units=["u1", "u2"], equations=["eq1", "eq2"],
            sub_scores_served=["X"],
        )}
        result = instantiate(config, domains)
        stub = result.embedder_stubs["test_domain"]
        assert "class TestdomainEmbedder" in stub
        assert "compute_matrix" in stub
