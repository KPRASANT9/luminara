# Implement a HAPOS Plan

Implement a feature from a planning document, respecting all invariants and physical laws.

## Instructions

Read the planning document at: $PLAN_PATH

For each task in the plan:

1. **Load context**: Read the relevant context files (hapos-laws.md, sub-scores.md, data-models.md)
2. **Check patterns**: Read existing code in the relevant src/ directory for consistency
3. **Implement as @coder**: Write the code following all patterns in the coder agent definition
4. **Test as @tester**: Write tests including invariant tests for the affected invariants
5. **Verify physics as @physicist**: If physics code changed, verify equations and units
6. **Run invariants as @invariant_runner**: Execute `pytest tests/invariants/ -v`
7. **Review as @reviewer**: Run the full review checklist

## Execution Order
```
@architect reads plan → decomposes into ordered task list
For each task:
  @coder implements (code + tests)
  @tester verifies test coverage
  @physicist validates physics (if applicable)
  @invariant_runner runs all 8 invariants
  @reviewer reviews against checklist
  @guardian audits security (if data handling)
  @documenter updates docs
```

## Gate: Do NOT proceed to the next task if any invariant fails on the current task.

RUN cat $PLAN_PATH
