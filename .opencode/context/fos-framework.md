# F-OS (Free-energy Operating System) — Agent Context

## The Universal Pattern
F-OS generalizes HAPOS. Any domain that can define physics (governing equations),
dimensions (sub-scores), and invariants (what must hold) gets the ENTIRE
self-improving architecture for free.

## Domain Matrix Innovation
Each domain embedder produces M_d ∈ ℝ^(k×n) — NOT scalars.
  k = physics state variables (rows have names, units, equations)
  n = timescale tiers (T1-T6)
A ProjectionMatrix W_d ∈ ℝ^(m×k) maps M_d → Φ partition.
W_d is optimized by the Embedding Ratchet (Level 2).

## Three Optimization Levels
  Level 1: Physics params → changes M_d content     (Loop 2 / Ratchet 2)
  Level 2: Projection W_d → changes what synth sees (Embedding Ratchet)  
  Level 3: Composition Π → changes how Φ_d compose  (Loop 3 / Ratchet 3)

## Instantiation
  /instantiate <domain> — generates embedders, projections, invariants, agents
  Inherits: 3×3 matrix, ratchets, loops, protocol, invariant framework
  Defines: physics, dimensions, signals, domain-specific invariants

## 9 Universal Agent Roles
  capturer → canonicalizer → embedder → coupler → synthesizer → feedback
       ↑                                                    ↓
       └──── refiner ←── tuner ←── auditor ←────────────────┘

## Communication: Each role has 1 input type + 1 output type
  capturer:       sensor_api → cloud_event
  canonicalizer:  cloud_event → aggregate_row
  embedder:       aggregate_row → domain_matrix
  coupler:        domain_matrix → kappa_vector
  synthesizer:    Φ + Κ + Π → objective_result (F, ∇F, ∇²F, N, I)
  feedback:       objective_result → closed_prediction
  refiner:        closed_prediction + ∇²F → model_version + projection_update
  tuner:          model_versions + drift → precision_vector (3 surfaces)
  auditor:        predictions + invariants → audit_result
