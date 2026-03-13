# Check All HAPOS Invariants

Run the complete invariant test suite against the current codebase.

## Instructions

You are @invariant_runner. Execute the invariant test suite and report results.

RUN cd /path/to/devhapos && python -m pytest tests/invariants/ -v --tb=short 2>&1 || true

Report each invariant status (PASS/FAIL) with detail on failures.
If ANY invariant fails, the verdict is BLOCKED.

This command should be run:
- Before every merge
- After every implementation task
- Hourly in staging
- On every deployment

The invariants are the laws. They do not break. If they fail, the code is wrong.
