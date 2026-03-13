# /autotune — Run Autonomous Parameter Optimization

Start the autoresearch ratchet loop on HAPOS objective functions.

## Instructions

You are @autotuner. Execute the three-ratchet loop:

### Ratchet 1 (S3-L1): Simulation — run immediately
1. Load current TwinState and model parameters
2. Generate 1000 candidate parameter perturbations (guided by Π)
3. Simulate HAP for each candidate
4. Rank by ΔHAP, filter by I4 (gradient validity)
5. Queue top 10 for Ratchet 2 validation

### Ratchet 2 (S3-L2): Validation — run weekly
1. Collect the week's prediction-outcome pairs from prediction_log
2. Retrodict: what would top candidates have predicted?
3. For each candidate: if retrodiction improves AND I7 passes → KEEP
4. Otherwise → DISCARD
5. Deploy kept version to NATS KV

### Ratchet 3 (S3-L3): Meta-tuning — run monthly
1. Analyze Loop 2 history: info gain per domain
2. Analyze coupling drift
3. Update Π: exploration precision, coupling trust, horizon
4. Verify I8 (calibration within 1σ) → deploy or reset to neutral

Target: $TARGET (energy | recovery | cognitive | agency | physics | all)

NEVER STOP. Log every experiment. Keep or discard.

RUN echo "AutoTune target: $TARGET"
