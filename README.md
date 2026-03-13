# DevHAPOS — HAPOS Development Environment

A self-improving intelligence system faithful to the physics that birthed life,
built with OpenCode multi-agent orchestration.

## Architecture

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                         DevHAPOS Agent Orchestration                        │
├──────────┬───────────┬───────────┬───────────┬───────────┬─────────────────┤
│@architect│  @coder   │ @reviewer │  @tester  │@physicist │   @guardian     │
│ Plans &  │Implements │ Invariant │  Tests    │  Physics  │   Security     │
│decomposes│  code     │  review   │  pyramid  │validation │   & privacy    │
└──────────┴───────────┴───────────┴───────────┴───────────┴─────────────────┘
                                    │
                        ┌───────────┴───────────┐
                        │  @invariant_runner     │
                        │  Gates ALL merges      │
                        │  8 invariants          │
                        └───────────────────────┘
```

## The Five Physical Laws

Every line of code exists to serve these laws:

| Law | Equation | Software Manifestation |
|-----|----------|----------------------|
| I: Information Conservation | H(output) ≥ H(input) | NATS append-only log. CloudEvent immutability. |
| II: Compositional Physics | Φ(system) = Compose(φ₁..φₙ) | LangGraph agents. Κ vector. Neo4j. |
| III: Temporal Conservation | ∂ρ/∂t + ∇·J = 0 | Temporal.io exactly-once. State snapshots. |
| IV: Self-Correction | ΔF → event → model update | Feedback loop. Prediction log. Loop 2. |
| V: Self-Improvement | F̄₃₀d(v_n) ≤ F̄₃₀d(v_{n-1}) | model_refinement_protocol. precision_controller. |

## Quick Start

```bash
# Install OpenCode
curl -fsSL https://opencode.ai/install | bash

# Clone and setup
git clone https://github.com/KPRASANT9/devhapos.git
cd devhapos
pip install -e ".[dev]"

# Start OpenCode with DevHAPOS agents
opencode
```

## OpenCode Commands

| Command | Description |
|---------|-------------|
| `/plan <feature>` | Create physics-aware planning document (4-D Appreciative) |
| `/implement <plan>` | Implement from plan with invariant gates |
| `/check-invariants` | Run all 8 HAPOS invariants |
| `/check-physics` | Run physics regression tests |
| `/review <files>` | Multi-agent review (@reviewer + @physicist + @guardian) |
| `/sprint-status` | Current sprint deliverables + invariant health |
| `/add-sensor <type>` | Add new sensor adapter with CloudEvent schema |
| `/add-agent <domain>` | Add new domain agent |
| `/loop2-status` | Loop 2 model refinement status |

## Agent Workflow

```
/plan "Add Kronauer circadian model"
  → @architect creates planning doc with sub-score mapping + invariant requirements
  
/implement docs/planning/PLAN-20260312-kronauer.md
  → @coder implements Kronauer model + TwinState writer
  → @tester writes physics regression + invariant I4 test
  → @physicist validates equations, units, reference bounds
  → @invariant_runner runs all 8 invariants
  → @reviewer reviews against law/invariant checklist
  → @guardian checks data handling
  → @documenter updates module docstrings
```

## Four Sub-Scores

| Sub-Score | Physics | Φ Partition | Agent |
|-----------|---------|-------------|-------|
| Energy (E) | Negentropy production | Φ_E[~20 dims] | Sleep/Circadian + Metabolic |
| Recovery (R) | PID + allostatic load | Φ_R[~15 dims] | Recovery |
| Cognitive (C) | Predictive precision (vmHRV) | Φ_C[~8 dims] | Cognitive |
| Agency (A) | Policy effectiveness | Φ_A[~8 dims] | Agency |

## Project Structure

```
devhapos/
├── AGENTS.md                        # Project rules (laws, invariants, standards)
├── .opencode/
│   ├── opencode.json               # Agent + command config
│   ├── agent/                       # 8 agent definitions
│   │   ├── architect.md            # Plans, decomposes, delegates
│   │   ├── coder.md                # Implements with invariant awareness
│   │   ├── reviewer.md             # Reviews against laws + invariants
│   │   ├── tester.md               # Test pyramid + invariant tests
│   │   ├── physicist.md            # Validates physics models
│   │   ├── guardian.md             # Security + privacy audit
│   │   ├── invariant_runner.md     # Gates all merges (8 invariants)
│   │   └── documenter.md           # Keeps docs in sync with code
│   ├── commands/                    # 9 slash commands
│   └── context/                     # HAPOS knowledge base for agents
│       ├── hapos-laws.md           # 5 physical laws
│       ├── sub-scores.md           # 4 sub-score equations
│       ├── invariants.md           # 8 invariants
│       ├── data-models.md          # CloudEvent, TwinState, etc.
│       ├── resilience-patterns.md  # 10 resilience patterns
│       └── coding-standards.md     # Python standards
├── src/
│   ├── pipeline/                    # Core: CloudEvent, TwinState, HAPResult
│   ├── connectors/                  # Sensor adapters (Oura, Whoop, Dexcom)
│   ├── physics/                     # Mechanistic models
│   │   ├── kronauer.py             # Circadian (E, R)
│   │   ├── bergman.py              # Glucose (E)
│   │   ├── banister.py             # Training (R)
│   │   ├── mcewan.py               # Allostatic load (R)
│   │   └── pid.py                  # Recovery controller (R)
│   ├── twin/                        # Digital Twin (NATS KV TwinState)
│   ├── coupling/                    # Cross-domain interaction (Κ vector)
│   ├── synthesizer/                 # HAP synthesis + gradient
│   ├── agents/                      # Domain agents (Sleep, Metabolic, etc.)
│   ├── temporal_workflows/          # Temporal.io workflows
│   ├── loop2/                       # model_refinement_protocol
│   └── loop3/                       # precision_controller
├── tests/
│   ├── invariants/                  # 8 invariant test files
│   ├── physics/                     # Physics regression tests
│   ├── integration/                 # Cross-service contract tests
│   └── e2e/                         # End-to-end pipeline tests
├── infra/                           # Docker Compose, K8s configs
└── docs/
    ├── architecture/                # ADRs
    └── planning/                    # Appreciative 4-D planning docs
```

## The Execution Guarantee

The development process mirrors the product:
- **Loop 1 (Weekly)**: Sprint → demo → measure against invariants
- **Loop 2 (Biweekly)**: Retro → what did we learn? → update priorities
- **Loop 3 (Quarterly)**: Is the process working? → adjust cadence, teams, depth

If the sprint plan is wrong, Loop 1 discovers it in 2 weeks.
If the architecture is wrong, Loop 2 discovers it in a month.
If the team structure is wrong, Loop 3 discovers it in a quarter.

**Nothing persists uncorrected.**
