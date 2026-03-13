# HAPOS Coding Standards — Agent Context

- Python 3.12+. Async-first: all I/O uses asyncio.
- Type hints on ALL public functions. mypy --strict must pass.
- ruff for linting. ruff format for formatting.
- Every module docstring: sub-score(s), law(s), invariant(s), team.
- Every CloudEvent: sub_score_targets populated.
- Every physics quantity: explicit units in variable name or docstring.
- Every model output: confidence interval included.
- Every external API call: circuit breaker pattern.
- Every missing data: Optional[T], never fabricate.
- Commits: [team] scope: description. State invariant/sub-score impact.
