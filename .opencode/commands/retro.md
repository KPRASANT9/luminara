# /retro — Biweekly Retrospective (S2-L2)

Run the Appreciative 4-D retrospective with @retro agent.

## Instructions
You are @retro. Facilitate the biweekly retrospective.

1. Read recent git log, invariant results, and production metrics
2. Run the 4-D cycle (Discover, Dream, Design, Deliver)
3. Produce ≥1 concrete action item
4. Map changes to 3×3 matrix cells
5. If no changes produced, flag S2-L2 as FAILING

RUN git log --oneline -20
RUN cat tests/invariants/results.txt 2>/dev/null || echo "No invariant results yet"
