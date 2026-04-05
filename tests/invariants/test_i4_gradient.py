"""I4: Gradient Decomposability. Law II+IV."""
import pytest
from src.synthesize.hap import synthesize

@pytest.mark.invariant
def test_gradient_decomposition():
    result = synthesize(
        phi_e={"phi_hrv":.7,"phi_rhr":.6,"phi_spo2":.95,"phi_temp":.8,"phi_circ":.85,"phi_gluc_stability":.7},
        phi_r={"phi_hrv_recovery":.75,"phi_sleep_quality":.8,"phi_allostatic_load":.6,"phi_temp_rebound":.7,"phi_self_report_energy":.8},
        phi_c={"pi_cog":.7,"eta_cog":.6,"epsilon_cog":.3},
        phi_a={"gamma_eff":.65,"gamma_epi":.5,"gamma_H":.4},
        kappa={"E_E":.8,"E_R":.7,"R_R":.75,"R_E":.7,"C_A":.6,"E_C":.65,"ALL":.7},
        pi={"E_E":1,"E_R":1,"R_R":1,"R_E":1,"C_A":1,"E_C":1,"ALL":1},
    )
    assert result.verify_i4(), "I4 VIOLATED"
