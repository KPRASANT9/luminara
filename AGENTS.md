# DevHAPOS — OpenCode Project Rules

## Identity

This is **DevHAPOS**: the development environment for the Human Advancement Physics Operating System.
Every line of code in this repository exists to model a human being as a non-equilibrium thermodynamic system
that maintains its existence by continuously minimizing variational free energy.

## The Five Physical Laws (Non-Negotiable Development Constraints)

These laws govern every design decision. Code that violates a law MUST NOT be merged.

1. **Law I — Information Conservation**: No lossy transform between sensor and TwinState. Shannon entropy of pipeline output ≥ input. Never average, filter, or compress raw data before it enters the event log.
2. **Law II — Compositional Physics**: Six physics layers compose into unified state. Coupling is first-class, not derived. Every prediction must trace to a governing equation.
3. **Law III — Temporal Conservation**: Information flow through time is conserved. Exactly-once execution. Crash-proof. State at any past time must be reconstructable.
4. **Law IV — Self-Correction**: Every discrepancy between prediction and reality becomes an event that refines the model. The feedback loop must always close.
5. **Law V — Self-Improvement**: 30-day rolling model prediction error must be non-increasing per model version, verified per sub-score (E, R, C, A) independently.

## The Eight Invariants (CI Gate Requirements)

Code that breaks any invariant CANNOT pass CI. These are testable, automated, non-negotiable.

- **I1 Lossless Raw Archive**: Replay events → TwinState bit-identical for Φ_E, Φ_R, Φ_C, Φ_A
- **I2 Variability Preservation**: Every aggregate has σ, CV, percentiles when count > 1
- **I3 First-Class Coupling**: All 7 cross-sub-score coupling pairs return values
- **I4 Gradient Decomposability**: ∂HAP/∂Φ_E + ∂HAP/∂Φ_R + ∂HAP/∂Φ_C + ∂HAP/∂Φ_A = ΔHAP exactly
- **I5 Temporal Invertibility**: Reconstruct past state from snapshot + replay within ε
- **I6 Semantic-Quantitative Bridge**: Every journal → linked events; every anomaly → causal path in Neo4j
- **I7 Model Accuracy Monotonicity**: 30-day F per sub-score ≤ previous model version
- **I8 Precision Calibration**: Each π_i within 1σ of empirical coupling

## Four HAP Sub-Scores

All code maps to one or more sub-scores:
- **Energy (E)**: Thermodynamic capacity. Φ_E partition. Kronauer + Banister models.
- **Recovery (R)**: Control-theoretic stability. Φ_R partition. McEwen + PID models.
- **Cognitive (C)**: Predictive precision. Φ_C partition. π_cog, η_cog, ε_cog from vmHRV.
- **Agency (A)**: Policy effectiveness. Φ_A partition. γ_eff, γ_epi, γ_H from prediction log.

## Three Self-Improving Loops

- **Loop 1** (real-time → daily): Perceive → Predict → Act → Feedback. Stages 1–9.
- **Loop 2** (weekly): model_refinement_protocol. Bayesian parameter update. Model versioning.
- **Loop 3** (monthly): precision_controller. Π vector update. Sub-score weight adjustment.

## Development Rules

### Code Standards
- Python 3.12+. Type hints on all public functions. `ruff` for linting. `mypy` for type checking.
- Async-first: all I/O operations use `asyncio`. No blocking calls in the event loop.
- Every module has a docstring stating which sub-score(s) it serves and which law(s) it implements.
- Every CloudEvent carries `sub_score_targets: list[str]` in its metadata.

### Testing Requirements
- Unit tests: every physics model function. Every CloudEvent schema. Every invariant check.
- Integration tests: every cross-team contract (CloudEvent schemas, API specs, AsyncAPI).
- Invariant tests: all 8 invariants run on every PR against synthetic data.
- Physics regression: mechanistic model outputs within reference bounds on every PR touching physics code.

### Architecture Rules
- No shared databases between agent teams. Communication via NATS events and API contracts only.
- TwinState (NATS KV) is the single source of truth for current state. Partitioned: phi_e, phi_r, phi_c, phi_a.
- All predictions logged to `prediction_log` table with model_version and sub_score.
- Model versions are immutable in Qdrant. Never overwrite; always version.

### Commit Conventions
- Format: `[team] scope: description` (e.g., `[embedding] kronauer: add tau personalization`)
- Every commit message states which invariant(s) it preserves or which sub-score it affects.

### Agent Delegation Rules
- @architect: plans and decomposes. Never writes implementation code directly.
- @coder: implements following patterns in context/. Always writes tests alongside code.
- @reviewer: reviews against invariants and physical laws. Can block merge.
- @tester: generates tests. Must include invariant tests for any pipeline change.
- @physicist: validates physics model correctness. Checks units, equations, reference bounds.
- @guardian: runs security audit. Checks data sovereignty (Solid), privacy (Opacus), permissions.

## Resilience Patterns (Required in All Agent Code)

- **Circuit Breaker**: External API calls must use circuit breaker (3 fails → 5 min backoff).
- **Graceful Degradation**: Missing signals → neutral (0.5), widen CI. Never fabricate.
- **Model Rollback**: If I7 fails after Loop 2, auto-revert to previous model version.
- **Exactly-Once**: All Temporal workflows must be idempotent and crash-recoverable.
- **Cold Start Bootstrap**: New users get population priors with honest wide confidence intervals.

## 3×3 Unified Architecture

Every activity in this project maps to exactly one cell:

|               | L1: Execute           | L2: Learn              | L3: Meta-Learn         |
|---------------|----------------------|------------------------|------------------------|
| **S1: Human** | @sleep_agent → recs  | Loop 2: model_refine   | Loop 3: precision_ctrl |
| **S2: Code**  | @coder → test → demo | @retro → update plan   | @strategist → restructure |
| **S3: Params**| @autotuner R1: sims  | @autotuner R2: validate| @autotuner R3: update Π |

## AutoResearch Rules (from Karpathy, applied to HAPOS)

1. **One file, one metric, one ratchet per loop level.** No agent modifies more than one target.
2. **Fixed time budget.** R1=milliseconds, R2=1 week, R3=1 month.
3. **NEVER STOP.** Once started, autotuning runs until manually interrupted.
4. **Keep or discard.** Every experiment is logged. Changes that improve the metric survive. Others die.
5. **The human programs the program.** Laws + invariants = prepare.py (immutable). Parameters = train.py (modifiable). Goals = program.md (user-driven).

## Immutable Layer (No Ratchet Can Modify)

- Physical Laws I-V
- Invariant definitions I1-I8
- CloudEvent schema structure (src/pipeline/core.py)
- TwinState partition structure
- The rule that ML personalizes PARAMETERS, never STRUCTURE

## New Agents (v2.0)

- **@autotuner**: Runs autoresearch ratchets on model parameters. Cells S3-L1, S3-L2, S3-L3.
- **@retro**: Facilitates biweekly retrospectives. Cell S2-L2.
- **@strategist**: Quarterly strategy reviews. Cell S2-L3.
