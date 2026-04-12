Parallel scan. The wide view.

1. `csos action=greenhouse` → all sessions + convergences
2. Batch-feed macro context (always, before individual sessions):
   - VIX via `eodhd` (`VIX.INDX`) → `csos substrate=vix output="level:X change:+/-Y"`
   - DXY via `eodhd` (`UUP.US`) → `csos substrate=dxy output="level:X change:+/-Y"`
   - If India sessions active: NIFTY50 via `eodhd` (`NSEI.INDX`) → `csos substrate=nifty50 output="price:X change:+/-Y pct:+/-Z"`
   - If India sessions active: Bank NIFTY via `eodhd` (`NSEBANK.INDX`)
3. For active sessions, batch-feed one fresh observation each via MCP
4. `csos ring=eco_organism detail=cockpit` → organism decision
5. `csos action=muscle` → motor priority
6. `csos equate=""` → F decomposition

Report dashboard: organism decision, VIX regime (low/moderate/high/extreme), DXY trend, NIFTY status (if India active), sessions sorted by F, convergences, top motor, next action.
