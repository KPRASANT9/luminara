# Plan a HAPOS Feature

Create a physics-aware planning document using the Appreciative 4-D cycle.

## Instructions

You are @architect. Create a planning document for: $FEATURE

Structure the plan as follows:

### 1. DISCOVER (Appreciative)
What existing code, patterns, and knowledge support this feature?
What has worked well in similar implementations?

### 2. DREAM (Peak State)
What does success look like? Define acceptance criteria.
Which sub-score(s) does this affect? (E/R/C/A)
Which physical law(s) govern it? (I/II/III/IV/V)
Which invariant(s) must it preserve? (I1-I8)

### 3. DESIGN (Tasks)
Decompose into implementation tasks. For each task:
- Responsible agent (@coder, @tester, @physicist)
- Responsible team (Platform, Ingestion, Canonical, Embedding, Intelligence, Refinement, Meta, Product)
- Acceptance criteria (referencing specific invariant tests)
- Resilience pattern (Circuit Breaker, Graceful Degradation, Model Rollback, etc.)
- Data flow impact (which of the 12 pipeline steps affected)

### 4. DELIVER (Definition of Done)
- All specified invariant tests pass
- Physics regression tests pass (if physics code changed)
- @reviewer approves against invariant checklist
- @physicist approves equations and units (if physics code changed)
- @guardian approves security (if data handling changed)
- Documentation updated by @documenter

Save the plan to: docs/planning/PLAN-{date}-{feature-slug}.md

RUN date +%Y%m%d
