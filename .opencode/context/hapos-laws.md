# HAPOS Physical Laws — Agent Context

## Law I: Information Conservation
- Equation: H(output) ≥ H(input)
- Rule: No lossy transform between sensor and TwinState
- Software: NATS JetStream append-only log. CloudEvent immutability.
- Invariants: I1 (Lossless Raw Archive), I2 (Variability Preservation)

## Law II: Compositional Physics
- Equation: Φ(system) = Compose(φ₁..φₙ)
- Rule: Six physics layers compose. Coupling is first-class. Every prediction traces to equation.
- Software: LangGraph agents compose. Κ vector in TwinState. Neo4j causal chains.
- Invariants: I3 (Coupling), I4 (Gradient), I6 (Semantic-Quantitative Bridge)

## Law III: Temporal Conservation
- Equation: ∂ρ/∂t + ∇·J = 0
- Rule: Exactly-once. Crash-proof. Past state reconstructable.
- Software: Temporal.io workflows. State snapshots. Continue-As-New.
- Invariants: I5 (Temporal Invertibility)

## Law IV: Self-Correction
- Equation: ΔF(prediction, reality) → event → model update
- Rule: Every prediction-reality discrepancy becomes a refining event.
- Software: Feedback → Event Bus. Prediction log. Loop 2 consumption.
- Invariants: I7 (Model Accuracy Monotonicity)

## Law V: Self-Improvement
- Equation: F̄₃₀d(v_n) ≤ F̄₃₀d(v_{n-1})
- Rule: Model accuracy non-increasing. Verified per sub-score.
- Software: model_refinement_protocol + precision_controller + Self-Improvement Audit
- Invariants: I7, I8 (Precision Calibration)
