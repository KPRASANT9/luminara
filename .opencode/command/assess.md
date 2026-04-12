Monthly health assessment for $SUBSTRATE. The diagnostic view.

Load `/skill econophysics` first.

Purpose: Answer "Is this company getting healthier or sicker?" independent of price.
Run monthly for portfolio holdings. Run before entry for new positions.

### STEP 1: WORTH (Is the company growing?)

`financialdatasets` getIncomeStatements for $SUBSTRATE (last 4 quarters)
→ Extract: revenue trend (Q/Q), EPS trend, gross margin trend
→ `csos substrate=${SUBSTRATE}_worth output="rev_growth:+/-X% eps_growth:+/-Y% margin_trend:+/-Z%"`

Direction: all three rising = strong growth. Mixed = stalling. All falling = decay.

### STEP 2: HEALTH (Can it sustain growth?)

`financialdatasets` getBalanceSheets for $SUBSTRATE (latest)
→ Extract: total_debt, total_cash, current_ratio, debt_to_equity
→ `csos substrate=${SUBSTRATE}_health output="debt_equity:X current_ratio:Y cash:Z debt:W"`

Flags: debt_equity > 2.0 = overleveraged. current_ratio < 1.0 = liquidity risk.

### STEP 3: TRAFFIC (Is attention growing?)

`eodhd` or `alphavantage` volume data for $SUBSTRATE (30-day avg vs current)
→ `csos substrate=${SUBSTRATE}_traffic output="vol_now:X vol_avg:Y vol_ratio:+/-Z"`

Direction: vol_ratio > 1.3 = surging interest. < 0.7 = fading attention.

### STEP 4: POTENTIAL (What could it capture?)

`financialdatasets` getIncomeStatements (revenue) + getNews (catalysts)
→ Compute: revenue run-rate = latest_quarterly × 4
→ Compare to sector peers (from `/rotate` sector data)
→ `csos substrate=${SUBSTRATE}_potential output="run_rate:X peer_growth:Y moat:strong/weak news_catalyst:+/-Z"`

Moat signals: margin > sector avg = strong. Margin declining toward sector avg = weak.

### STEP 5: CATALYST SCAN (What's driving bullish/bearish?)

Feed all four substrates into greenhouse, then decompose:
```
csos action=greenhouse
```

Read convergences for $SUBSTRATE:
- Converges with growth substrates → bullish catalyst
- Converges with decay substrates → bearish catalyst
- No convergences → isolated / no clear catalyst

`financialdatasets` getInsiderTrades → insider conviction signal
`financialdatasets` getNews → sentiment + hype detection
→ `csos substrate=${SUBSTRATE}_catalyst output="insider_net:+/-X news_sentiment:+/-Y hype:high/low org_decay:true/false"`

Hype detection: positive news + declining fundamentals = hype (bearish despite sentiment).
Org decay: declining margins + insider selling + rising debt = organizational decay.

### SYNTHESIS — Monthly Health Card

```
csos ring=${SUBSTRATE}_worth detail=cockpit
csos ring=${SUBSTRATE}_health detail=cockpit
csos ring=${SUBSTRATE}_traffic detail=cockpit
csos ring=${SUBSTRATE}_potential detail=cockpit
csos ring=${SUBSTRATE}_catalyst detail=cockpit
```

Present:
```
═══ MONTHLY HEALTH: $SUBSTRATE ═══
WORTH:     ▲/▼ (rev +X%, eps +Y%, margin +Z%)
HEALTH:    ▲/▼ (D/E: X, current: Y, cash: $Z)
TRAFFIC:   ▲/▼ (vol ratio: X — surging/normal/fading)
POTENTIAL: ▲/▼ (run rate: $X, moat: strong/weak)
CATALYST:  BULLISH/BEARISH/NEUTRAL
  → Primary: [insider buying / revenue acceleration / margin expansion / etc.]
  → Risk:    [hype disconnect / org decay / overleveraged / etc.]
OVERALL:   HEALTHY / WATCH / DETERIORATING
```

HEALTHY = 4+ layers ▲. WATCH = mixed. DETERIORATING = 3+ layers ▼.

If DETERIORATING + currently holding → flag for exit review via `/check`.
If HEALTHY + not holding → flag as candidate for `/focus` deep dive.
