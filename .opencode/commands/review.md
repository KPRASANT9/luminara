# Multi-Agent Review

Run a comprehensive review of the specified files using three specialist agents in parallel.

## Instructions

Review files: $FILES

### Agent 1: @reviewer
Run the full invariant and law compliance checklist.
Output: APPROVE / REQUEST CHANGES / BLOCK with reasons.

### Agent 2: @physicist
If any physics model code is touched:
- Validate equations against references
- Check dimensional consistency
- Verify confidence intervals present
- Confirm ML personalizes parameters only (never structure)
Output: APPROVE / BLOCK with physics-specific feedback.

### Agent 3: @guardian
If any data handling code is touched:
- Check Solid Protocol compliance
- Verify no data leakage paths
- Confirm privacy budget tracking
- Audit permission boundaries
Output: APPROVE / BLOCK with security-specific feedback.

### Synthesis
All three must APPROVE for merge to proceed.
Any BLOCK from any agent = merge BLOCKED.

RUN git diff --name-only HEAD~1
