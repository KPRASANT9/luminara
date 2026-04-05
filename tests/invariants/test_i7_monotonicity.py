"""I7: Model Accuracy Monotonicity. Law V."""
import pytest
from src.loop2.refinement import ModelRefinement, ModelVersion

@pytest.mark.invariant
@pytest.mark.asyncio
async def test_ratchet2_rejects_regressions():
    current = ModelVersion(params={"tau":24.2}, error_30d={"E":.10,"R":.12,"C":.15,"A":.18})
    protocol = ModelRefinement(current)
    bad = ModelVersion(params={"tau":24.5}, error_30d={"E":.15,"R":.11,"C":.14,"A":.17})
    assert not await protocol.evaluate(bad, []), "I7: worsening E must be rejected"
    good = ModelVersion(params={"tau":24.15}, error_30d={"E":.08,"R":.10,"C":.13,"A":.16})
    assert await protocol.evaluate(good, []), "I7: all-improving candidate must be kept"
