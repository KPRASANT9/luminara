"""I8: Precision Calibration. Loop 3 Π within 1σ."""
import pytest
from src.loop3.precision import PrecisionController, PrecisionVector

@pytest.mark.invariant
@pytest.mark.asyncio
async def test_precision_resets_on_miscalibration():
    ctrl = PrecisionController()
    bad_emp = {"E_E":5.0,"E_R":5.0,"R_R":5.0,"R_E":5.0,"C_A":5.0,"E_C":5.0,"ALL":5.0}
    new_pi = await ctrl.update({}, {}, bad_emp)
    assert new_pi.horizon == PrecisionVector.neutral().horizon
