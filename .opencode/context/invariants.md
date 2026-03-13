# HAPOS Information Invariants — Agent Context

| ID | Name | Rule | Test | Gate |
|----|------|------|------|------|
| I1 | Lossless Raw Archive | Every sensor reading immutable in NATS | Replay → TwinState bit-identical | CI + Weekly prod |
| I2 | Variability Preservation | Aggregates: σ, CV, percentiles when count>1 | Query any (user,signal,tier) | CI + Hourly |
| I3 | First-Class Coupling | Κ stored as primary state dimension | All 7 pairs return values | CI + Daily |
| I4 | Gradient Decomposability | HAP + full ∇HAP always | Partition sum = ΔHAP | CI + Per computation |
| I5 | Temporal Invertibility | Past state reconstructable | Snapshot + replay within ε | CI + Weekly |
| I6 | Semantic-Quantitative Bridge | Journals↔biometrics, anomalies↔context | Neo4j path exists | CI + Daily |
| I7 | Model Accuracy Monotonicity | 30-day F ≤ previous per sub-score | Per E,R,C,A independently | CI + After Loop 2 |
| I8 | Precision Calibration | Π within 1σ of empirical | Per coupling pair | CI + After Loop 3 |
