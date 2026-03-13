"""I2: Variability Preservation. Law I+IV."""
import pytest
from src.core.types import SubScore
from src.canonicalize.aggregator import Aggregator, AggregateRow, CloudEvent

@pytest.mark.invariant
@pytest.mark.asyncio
async def test_aggregates_have_sigma_cv():
    agg = Aggregator()
    for v in [70.0, 72.0, 68.0, 71.0, 69.0]:
        event = CloudEvent(type="bio.cardiac.hr", subject="user:test",
                           value=v, sub_score_targets=(SubScore.ENERGY,))
        await agg.ingest(event)
    row = await agg.flush_tier("test", "bio.cardiac.hr", "T3", (SubScore.ENERGY,))
    assert row.count == 5
    assert row.sigma > 0, "I2: σ must be positive when count > 1"
    assert row.cv > 0, "I2: CV must be positive when count > 1"
