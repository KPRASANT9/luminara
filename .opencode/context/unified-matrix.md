# HAPOS 3×3 Unified Architecture — Agent Context

## The Matrix: Every activity maps to exactly one cell

|               | L1: Execute (Fast)      | L2: Learn (Medium)      | L3: Meta-Learn (Slow)   |
|---------------|------------------------|------------------------|------------------------|
| **S1: Human** | Agents→Recommend→Feedback | Loop 2: model_refinement | Loop 3: precision_ctrl  |
| **S2: Code**  | Sprint→Test→Demo        | Retro→Update priorities | Strategy→Restructure   |
| **S3: Params**| Ratchet 1: 1000 sims    | Ratchet 2: weekly valid  | Ratchet 3: monthly Π   |

## Cross-Cell Signals (every agent must know which signals it produces/consumes)
- S1-L1 → S3-L2: Prediction-outcome pairs (biology validates parameters)
- S3-L2 → S1-L1: Updated model parameters (product uses better models)
- S3-L3 → S3-L1: Updated Π (controls simulation search space)
- S3-L3 → S1-L3: Updated precision + weights (controls product behavior)
- S2-L1 → S1-L1: Deployed code (new features, fixes)
- S1-L1 → S2-L2: Production metrics (reality informs dev priorities)
- S2-L2 → S2-L1: Updated sprint priorities
- S2-L3 → S2-L2: Updated process parameters
- S2-L3 → S3-L3: Updated autotuning strategy

## AutoResearch Principles (from Karpathy)
1. **One file, one metric, one ratchet** per loop level
2. **Fixed time budget, rapid iteration** at each level
3. **The human programs the program** — not the code
4. **NEVER STOP** — loops run continuously
5. **Keep or discard** — the ratchet only turns one direction

## Immutable Layer (prepare.py — NEVER modified by any ratchet)
- Physical Laws I-V
- Invariant definitions I1-I8
- CloudEvent schema structure
- TwinState partition structure
- The fact that ML personalizes PARAMETERS, never STRUCTURE
