# /matrix-status — 3×3 Matrix Health Dashboard

Show the health of all 9 cells in the unified architecture.

## Instructions
For each cell in the 3×3 matrix, report:
- Current metric value
- Trend (improving / stable / degrading)
- Last invariant check result
- Active experiments (for S3 cells)

Format as a 3×3 grid matching the unified architecture.

RUN python -c "
from src.validators.invariants import run_all_invariants
from src.pipeline.core import TwinState
import asyncio
twin = TwinState(user_id='pilot-001')
results = asyncio.run(run_all_invariants('pilot-001', twin))
for r in results:
    status = '✅' if r.passed else '❌'
    print(f'{status} {r.id}: {r.name} — {r.detail}')
" 2>/dev/null || echo "Run 'pip install -e .' first to enable invariant checks"
