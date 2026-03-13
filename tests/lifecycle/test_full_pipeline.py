"""
Lifecycle Integration Test — data flows: capture → canonicalize → embed → couple → synthesize → feedback.
This is the most important test: if this passes, the pipeline is alive.
"""
import pytest
from src.core.types import CloudEvent, TwinState, PredictionLogEntry, SubScore
from src.core.state import StateManager
from src.canonicalize.aggregator import Aggregator
from src.embed.sleep import SleepEmbedder
from src.embed.metabolic import MetabolicEmbedder
from src.embed.cognitive import CognitiveEmbedder
from src.embed.agency import AgencyEmbedder
from src.couple.analyzer import compute_kappa
from src.synthesize.hap import synthesize
from src.feedback.protocol import FeedbackProtocol

@pytest.mark.asyncio
async def test_full_lifecycle():
    """One complete cycle through all 6 stages."""
    state_mgr = StateManager()
    user = "test-lifecycle"

    # STAGE 1: CAPTURE — raw events
    events = [
        CloudEvent(type="bio.cardiac.ibi", subject=f"user:{user}",
                   value=v, unit="ms", sub_score_targets=(SubScore.ENERGY, SubScore.COGNITIVE))
        for v in [800.0, 820.0, 790.0, 810.0, 805.0]
    ]
    assert all(len(e.sub_score_targets) > 0 for e in events), "All events have targets"

    # STAGE 2: CANONICALIZE — aggregate
    agg = Aggregator()
    for e in events:
        await agg.ingest(e)
    row = await agg.flush_tier(user, "bio.cardiac.ibi", "T3",
                               (SubScore.ENERGY, SubScore.COGNITIVE))
    assert row.count == 5
    assert row.sigma > 0, "I2: σ present"
    assert row.cv > 0, "I2: CV present"

    # STAGE 3: EMBED — physics models → Φ partitions
    sleep_emb = SleepEmbedder()
    sleep_result = sleep_emb.embed(hours_awake=14.0, light_lux=50.0)
    await state_mgr.write_phi(user, SubScore.ENERGY, sleep_emb.get_phi_e(sleep_result))
    await state_mgr.write_phi(user, SubScore.RECOVERY, sleep_emb.get_phi_r(sleep_result))

    met_emb = MetabolicEmbedder()
    await state_mgr.write_phi(user, SubScore.ENERGY, met_emb.embed([95, 110, 98, 105]))

    cog_emb = CognitiveEmbedder()
    phi_c = cog_emb.embed(rmssd=42.0, rmssd_baseline_30d=45.0,
                          rem_pct=0.22, model_improvement_rate=0.3)
    await state_mgr.write_phi(user, SubScore.COGNITIVE, phi_c)

    ag_emb = AgencyEmbedder()
    phi_a = ag_emb.embed(predictions=[], timing_entropy=0.4)
    await state_mgr.write_phi(user, SubScore.AGENCY, phi_a)

    # Fill missing components with defaults for test completeness
    twin = await state_mgr.get(user)
    for key in ["phi_hrv", "phi_rhr", "phi_spo2"]:
        twin.phi_e.components.setdefault(key, 0.7)
    for key in ["phi_hrv_recovery", "phi_allostatic_load", "phi_self_report_energy"]:
        twin.phi_r.components.setdefault(key, 0.7)

    # STAGE 4: COUPLE — cross-domain Κ
    kappa = compute_kappa(twin)
    await state_mgr.write_kappa(user, kappa)
    assert len(twin.kappa) == 7, "I3: 7 pairs"

    # STAGE 5: SYNTHESIZE — HAP score + gradient
    result = synthesize(
        twin.phi_e.components, twin.phi_r.components,
        twin.phi_c.components, twin.phi_a.components,
        twin.kappa, twin.pi or {"E_E":1,"E_R":1,"R_R":1,"R_E":1,"C_A":1,"E_C":1,"ALL":1},
    )
    assert 0 <= result.hap <= 100, f"HAP in range: {result.hap}"
    assert result.verify_i4(), "I4: gradient decomposes"

    # STAGE 6: FEEDBACK — close prediction loop
    fb = FeedbackProtocol()
    pred = PredictionLogEntry(
        user_id=user, sub_score=SubScore.ENERGY,
        domain="sleep", predicted_value=result.energy.score,
        model_version="v0.0.0",
    )
    await fb.record_prediction(pred)
    closed = await fb.close_prediction(pred.id, actual=result.energy.score + 2.0)
    assert closed is not None
    assert closed.prediction_error is not None, "Law IV: prediction closed"
    assert closed.prediction_error == pytest.approx(2.0, abs=0.1)

    print(f"\n✅ Full lifecycle passed: HAP={result.hap:.1f}, "
          f"E={result.energy.score:.1f}, R={result.recovery.score:.1f}, "
          f"C={result.cognitive.score:.1f}, A={result.agency.score:.1f}")
