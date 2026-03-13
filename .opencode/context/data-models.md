# HAPOS Data Models — Agent Context

## CloudEvent (src/pipeline/__init__.py)
- specversion: "1.0", id: UUIDv7, source: adapter URI, type: signal taxonomy
- subject: user WebID, time: sensor timestamp (NOT receipt), value: native units
- sub_score_targets: list[SubScore] — REQUIRED on every event
- raw_payload: original vendor payload preserved (Law I)

## TwinState (NATS KV: twin.{user_id})
- phi_e, phi_r, phi_c, phi_a: PhiPartition (one per sub-score)
- sigma: 1536-dim semantic vector (LLM embedded)
- delta: dynamics (derivatives at T3/T4/T5)
- kappa: 7 cross-sub-score coupling values
- pi: precision weights from Loop 3
- hap_score + hap_gradient_{e,r,c,a}
- model_version: current model version tag

## Signal Taxonomy (CloudEvent.type)
- bio.cardiac.* → E, R, C
- bio.metabolic.* → E, R
- bio.movement.* → R, A
- bio.cognitive.* → C, R
- user.subjective.* → R, C, A
- env.context.* → E, A
- system.internal.* → All
