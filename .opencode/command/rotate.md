Portfolio rotation across sectors. The strategic view.

Load `/skill econophysics` first.

**Detect market:** If user specifies India/NSE/NIFTY → India rotation. Otherwise → US rotation.

### US STEP 1: ASSESS (feed 17 substrates)
  Feed 6 macro forces via `alphavantage`:
    Fed (FEDERAL_FUNDS_RATE), Inflation (CPI), Employment (NONFARM_PAYROLL),
    Credit (TREASURY_YIELD), Trade (REAL_GDP), Oil (WTI)
  → `csos action=batch items='[6 macro signals with signed direction]'`

  Feed DXY via `eodhd` (`UUP.US` — dollar proxy ETF):
  → `csos substrate=dxy output="level:X change:+/-Y"`

  Feed VIX via `eodhd` (`VIX.INDX`):
  → `csos substrate=vix output="level:X change:+/-Y"`

  Feed 11 sector ETFs via `alphavantage` GLOBAL_QUOTE:
    XLK, XLF, XLV, XLE, XLI, XLP, XLY, XLC, XLRE, XLB, XLU
  → `csos action=batch items='[11 sector signals with relative performance]'`
  "relative" = sector return minus SPY return. This is the key signal.

### India STEP 1: ASSESS (feed India macro + sector indices)
  Feed macro context via `eodhd`:
    NIFTY 50 (`NSEI.INDX`), SENSEX (`BSESN.INDX`),
    DXY proxy (`UUP.US`), VIX (`VIX.INDX`)
  → `csos action=batch items='[4 macro signals with signed direction]'`

  Feed India sector indices via `eodhd` (VERIFIED tickers):
    `CNXIT.INDX` (IT), `NSEBANK.INDX` (Banking), `CNXPHARMA.INDX` (Pharma),
    `CNX100.INDX` (NIFTY 100 broad)
  → `csos action=batch items='[4 sector signals with relative performance]'`
  "relative" = sector index return minus NSEI.INDX (NIFTY 50) return.

  Optional US-listed India proxies for cross-market view:
    `INDA.US` (iShares MSCI India), `NFTY.US` (NIFTY 50 Equal Weight), `SMIN.US` (Small Cap)

**STEP 2: RANK** (read physics for each sector)
  `csos action=greenhouse` → convergences reveal regime structure
  For each sector: `csos ring=SECTOR detail=cockpit`
  Score = gradient × direction × (1-F)
  Sort: highest score = OVERWEIGHT, lowest = UNDERWEIGHT

**STEP 3: ALLOCATE** (F-weighted)
  Top 3 positive: weight ∝ score. These are LONG positions (sector ETFs).
  Bottom 3 negative: small SHORT allocation × conviction.
  Middle 5: SKIP (no edge, save transaction costs).

  Conviction = avg(1-F) across selected sectors.
  Total long = portfolio × 0.90 × conviction.
  Total short = portfolio × 0.10 × conviction.

**STEP 4: DURATION** (from regime HMM)
  `csos ring=eco_organism detail=cockpit` → active_regime
  Early cycle: expect 12-18mo. Mid cycle: 24-48mo. Late: 6-18mo. Recession: 6-18mo.
  Monitor at frequency inversely proportional to expected duration.

**STEP 5: PRESENT for confirmation**
  ```
  REGIME: {early/mid/late/recession} (confidence: {1-F})
  OVERWEIGHT: {sector1} {weight1}%, {sector2} {weight2}%, {sector3} {weight3}%
  UNDERWEIGHT: {sector1} {weight1}%, {sector2} {weight2}%
  DURATION: ~{months} months (from HMM persistence P={transition_prob})
  CATALYST: {strongest CausalAtom: macro_force → sector, lag, strength}
  ```

**STEP 6: EXECUTE** (on confirmation, via Alpaca)
  For each sector allocation: `alpaca submit_order symbol=ETF qty=N side=buy/sell`
  Feed ALL results: `csos substrate=rotation_exec output="..."`

**STEP 7: MONITOR** (daily /scan, watch for regime shift)
  3+ regime change signals → RE-ROTATE.
  Signals: coupling density spike, defensives outperform, VIX spike, credit widen.
