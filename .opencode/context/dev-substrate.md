# F-OS Development Substrate — Agent Context

## The Development Physics (prepare.py — immutable)

1. **Token Conservation**: cost = Σ(tokens × price). Can't compute for free.
2. **Context Window Bound**: H(output) ≤ C(context). Lost info is lost.
3. **Accuracy-Cost Frontier**: Pareto boundary between pass_rate and token_spend.
4. **Compositional Correctness**: Pipeline correct iff each stage correct AND contracts hold.

## The Four Dev Sub-Scores

| Sub-Score | Measures | Analogous HAPOS Sub-Score |
|---|---|---|
| Accuracy (A_dev) | Invariant pass rate per agent | Energy |
| Efficiency (E_dev) | Tokens per successful commit | Recovery |
| Coherence (C_dev) | Pattern + contract adherence | Cognitive |
| Velocity (V_dev) | Time to passing commit | Agency |

## Agent DomainMatrix M_agent ∈ ℝ^(5 × 8)

Each agent produces a performance matrix:
  Rows: accuracy, efficiency, coherence, velocity, token_cost
  Cols: plan, implement, review, test, physics_check, security_audit, document, autotune

ProjectionMatrix W_agent maps this into dev sub-scores.
W_agent is optimized by the Prompt Ratchet (Level 2).

## Three Optimization Levels

| Level | What | Optimizer | Timescale | Metric |
|---|---|---|---|---|
| 1: Model params | model_id, temperature, max_steps | Model Router | Per-task | Accuracy/cost score |
| 2: Prompts | Agent prompt content (W_d equivalent) | Prompt Ratchet | Daily | Pass rate per version |
| 3: Topology | Agent structure, routing, permissions | Architecture Evolver | Monthly | Accuracy-cost ratio |

## Unified Dev Objective

F_dev(t) = α · N_dev(t) + β · I_dev(t)

N_dev = development negentropy (working code production)
I_dev = development info gain (agent learning rate)
