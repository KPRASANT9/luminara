"""I3: First-Class Coupling. Law II."""
import pytest
from src.core.types import TwinState
from src.couple.analyzer import compute_kappa

@pytest.mark.invariant
def test_coupling_produces_7_pairs():
    twin = TwinState(user_id="test")
    twin.phi_e.components = {"phi_hrv": 0.7, "phi_rhr": 0.6}
    twin.phi_r.components = {"phi_sleep_quality": 0.8}
    twin.phi_c.components = {"pi_cog": 0.65}
    twin.phi_a.components = {"gamma_eff": 0.5}
    kappa = compute_kappa(twin)
    assert len(kappa) == 7, f"I3: {len(kappa)}/7"
