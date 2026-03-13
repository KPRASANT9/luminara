# F-OS Universal Agent Communication Protocol

## The 9 Agent Roles (map to 3×3 matrix)

Every F-OS instantiation has exactly these 9 roles.
Each role produces ONE output type and consumes ONE input type.
Communication medium: NATS events with targets metadata.

### Signal Flow (left-to-right = data flow):

```
capture → canonicalize → embed → couple → synthesize → feedback
                                                          ↓
                                          loop2 ← meta_gradient
                                            ↓
                                          loop3 ← loop2_history
                                            ↓
                                          autotune ← Π vector
```

### Agent IO Contracts

| Agent Role | Consumes | Produces | Matrix Cell |
|---|---|---|---|
| @capturer | External signals | Signal stream | S1-L1 |
| @canonicalizer | Signal stream | AggregateRow (T0-T6) | S1-L1 |
| @embedder(s) | Aggregates | DomainMatrix M_d + Φ dict | S1-L1 |
| @coupler | All DomainMatrices | KappaVector (7 pairs) | S1-L1 |
| @synthesizer | Φ + Κ + Π | ObjectiveResult (F, ∇F, ∇²F, N, I) | S1-L1 |
| @feedback | Predictions + outcomes | ClosedPrediction | S1-L1→L2 |
| @refiner | ClosedPredictions + ∇²F | ModelVersion (updated params + W_d) | S1-L2 |
| @tuner | L2 history + ∇²F | PrecisionVector Π | S1-L3 |
| @autotuner | Π + M_d + params | CandidateList (perturbations) | S3-L1 |

### Three Optimization Levels (Autoresearch-mapped)

```
Level 1: Physics params (τ, S_I, k_1)     → changes M_d CONTENT
         Optimizer: Loop 2 (Ratchet 2, weekly)
         Metric: prediction error per sub-score

Level 2: Embedding projection (W_d)        → changes what synthesize SEES
         Optimizer: Embedding Ratchet (daily, machine-speed)
         Metric: HAP prediction error after re-projection

Level 3: Composition weights (Π)           → changes how Φ's COMPOSE into F
         Optimizer: Loop 3 (Ratchet 3, monthly)
         Metric: calibration error + info gain efficiency
```

### For ANY New Domain (via /instantiate):

1. Define physics equations (compute_matrix)
2. Define sub-scores (which dimensions of F matter)
3. Define initial projection W_d (default_projection)
4. Define invariants (what must hold)
5. Everything else is inherited from F-OS:
   - Ratchet protocol
   - Loop mechanics
   - Invariant gates
   - Agent communication
   - 3×3 matrix tracking
