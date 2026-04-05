"""I1: Lossless Raw Archive. Law I."""
import pytest
from src.core.types import CloudEvent, SubScore

@pytest.mark.invariant
def test_cloud_event_immutability():
    event = CloudEvent(type="bio.cardiac.ibi", subject="user:001",
                       sub_score_targets=(SubScore.ENERGY,))
    with pytest.raises(AttributeError):
        event.value = 999  # type: ignore
