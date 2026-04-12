Deep focus on $SUBSTRATE. Seven layers + directional synthesis.

Load `/skill econophysics` first.

**Detect market:** If $SUBSTRATE ends in `.NSE`, `.BSE`, or `.INDX` → India path. Otherwise → US path.

### US path (default) — 10 layers

1. **Price**: `eodhd` real-time `$SUBSTRATE.US` → fallback `alphavantage` GLOBAL_QUOTE
   → `csos substrate=$SUBSTRATE output="price:X change:+/-Y pct:+/-Z vol:V"`

2. **Traffic**: Volume relative to 30-day avg (from price data)
   → `csos substrate=${SUBSTRATE}_traffic output="vol_now:X vol_avg:Y vol_ratio:+/-Z"`
   Direction: ratio > 1.3 = surging attention, < 0.7 = fading

3. **Trend**: `alphavantage` RSI + MACD (or `eodhd` technicals)
   → `csos substrate=${SUBSTRATE}_tech output="rsi:X macd:Y signal:Z hist:+/-H"`
   Direction: RSI>50 = bullish, MACD hist>0 = bullish

4. **Worth**: `financialdatasets` getIncomeStatements (4 quarters)
   → `csos substrate=${SUBSTRATE}_worth output="rev_growth:+/-X% eps_growth:+/-Y% margin:+/-Z%"`
   Direction: all rising = strong growth company

5. **Health**: `financialdatasets` getBalanceSheets
   → `csos substrate=${SUBSTRATE}_health output="debt_equity:X current_ratio:Y cash:Z"`
   Flags: D/E > 2.0 = overleveraged, current < 1.0 = liquidity risk

6. **Potential**: Revenue run-rate vs sector, margin vs peers
   → `csos substrate=${SUBSTRATE}_potential output="run_rate:X moat:strong/weak"`
   Moat: margin > sector avg = strong competitive position

7. **Flow**: `financialdatasets` getInsiderTrades
   → `csos substrate=${SUBSTRATE}_flow output="buys:X sells:Y net:+/-Z"`
   Direction: net>0 = bullish (insiders buying)

8. **Sentiment**: `financialdatasets` getNews
   → `csos substrate=${SUBSTRATE}_news output="positive:X negative:Y net:+/-Z"`
   Hype check: positive news + declining worth = hype (bearish signal)

9. **Microstructure**: `alpaca` get_latest_quote → bid, ask, sizes
   → `csos substrate=${SUBSTRATE}_book output="bid:X ask:Y imbalance:+/-Z"`
   Direction: imbalance>0 = bullish (bid-heavy)

10. **Context**: `csos action=greenhouse` → convergences + catalyst detection

### India path (when .NSE / .BSE / .INDX detected)

1. **Price**: `eodhd` real-time for $SUBSTRATE
   → `csos substrate=$SUBSTRATE output="price:X change:+/-Y pct:+/-Z vol:V"`

2. **Index context**: `eodhd` `NSEI.INDX` (NIFTY 50) + `NSEBANK.INDX` (Bank NIFTY)
   → `csos substrate=nifty_ctx output="nifty50:X niftybank:Y sector_bias:+/-Z"`

3. **Sector context**: `eodhd` pick relevant sector index:
   IT stocks → `CNXIT.INDX`, Bank/Finance → `NSEBANK.INDX`, Pharma → `CNXPHARMA.INDX`
   → `csos substrate=${SUBSTRATE}_sector output="sector_index:X change:+/-Y"`

4. **DXY impact**: `eodhd` `UUP.US` (dollar proxy ETF) → strong dollar hurts EM/India
   → `csos substrate=dxy_ctx output="level:X change:+/-Y india_bias:+/-Z"`
   Direction: UUP rising = bearish for India equities

5. **VIX regime**: `eodhd` `VIX.INDX`
   → `csos substrate=vix_ctx output="level:X change:+/-Y regime:low/mod/high/extreme"`
   VIX <15 = low fear, 15-20 = moderate, 20-30 = high, >30 = extreme

6. **Broad market**: `eodhd` `BSESN.INDX` (SENSEX) + `CNX100.INDX` (NIFTY 100)
   → `csos substrate=india_broad output="sensex:X nifty100:Y"`

7. **Context**: `csos action=greenhouse` → convergences

**SYNTHESIS** (after all layers):
`csos ring=$SUBSTRATE detail=cockpit` → decision, regime, gradient

Directional consensus (10 signals, 3 tiers):

  **Tier 1 — Structural (weight 0.40):**
    worth×0.15 + health×0.10 + potential×0.15
    These answer: "Should this company exist in your portfolio?"

  **Tier 2 — Momentum (weight 0.35):**
    tech×0.15 + traffic×0.10 + flow×0.10
    These answer: "Is the market agreeing with the thesis?"

  **Tier 3 — Tactical (weight 0.25):**
    sentiment×0.10 + book×0.10 + price×0.05
    These answer: "Is now the right moment?"

  consensus = Σ(direction_i × weight_i)
  |consensus|>0.5 → STRONG. 0.2-0.5 → MODERATE. <0.2 → WEAK.
  sign = LONG (positive) or SHORT (negative)

  **Catalyst check:** If sentiment positive BUT worth declining → HYPE (reduce conviction).
  If insider selling + health deteriorating → ORG DECAY (bearish override).

**Earnings check** (auto — if `financialdatasets` has data):
  → getEarnings for $SUBSTRATE → next date, consensus, beat/miss history
  → If within 14 days: flag, compute pre-earnings setup (BUY/FADE/SKIP based on health + hype)
  → If just reported: capture surprise, feed drift for 5 days, auto-tune
  → `csos substrate=${SUBSTRATE}_earnings output="days_to:N consensus:X beat_rate:Y surprise_avg:+/-Z"`

If EXECUTE: present direction, conviction (from F), size, stop (from rw).
If EXPLORE: which tier is weakest. Feed that tier next.
