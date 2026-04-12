---
name: econophysics
description: "Operative market physics. Direction prediction. Auto-tuning. F→0."
---

# Econophysics: Direction + Edge + Auto-Tune

V15 binary implements 14 mechanisms (7 light + 7 dark reactor). The skill layer closes the remaining 6 gaps for directional prediction and auto-tuning.

## What the Binary Already Does (14/14 mechanisms)

**Light reactor** (external → complexity ↓): info decay, self-impact, order book gradient, spectral weight, agent type classification, adversarial decay, photoprotection.

**Dark reactor** (internal → accuracy ↑): causal discovery, regime HMM (bull/bear/crisis), active inference targeting, counterfactual scoring, hierarchy L0→L4, rhythm detection, information-theoretic pruning.

## The 6 Gaps the Skill Layer Closes

### Gap 1: Direction — Feed SIGNED Deltas

The membrane resonates on magnitude, not direction. Fix: feed signed change, not just price.

```
WRONG:  csos substrate=AAPL output="price:182.50"
RIGHT:  csos substrate=AAPL output="price:182.50 change:+1.2 delta:+2.50"

WRONG:  csos substrate=vix output="level:18.2"
RIGHT:  csos substrate=vix output="level:18.2 change:-0.5 delta:-0.5"
```

The sign of `change` and `delta` feeds directional information into the gradient. Atoms that see consistent positive deltas build a DIRECTIONAL gradient. The causal chain preserves direction: if cause has positive delta and effect historically follows with positive delta, the CausalAtom encodes `direction=+1`.

**When reading cockpit**, infer direction from:
- gradient trajectory (rising fast = strong signal in recent direction)
- regime (bull=0, bear=1, crisis=2 from `active_regime`)
- causal chain direction (causal atoms encode `causal_direction`)

### Gap 2: Direction Synthesis → BUY/SELL

Boyer says EXECUTE/EXPLORE. You synthesize direction:

```
After /check AAPL shows decision=EXECUTE:

1. Read regime: csos ring=eco_organism detail=cockpit → active_regime
   0=bull (bias LONG), 1=bear (bias SHORT), 2=crisis (bias FLAT)

2. Read causal chain: check greenhouse convergences
   If AAPL converges with positive-delta substrates → LONG
   If AAPL converges with negative-delta substrates → SHORT

3. Read motor momentum: csos action=muscle
   If AAPL motor strength > 0.7 with recent positive deltas → LONG confirmed

4. SYNTHESIZE:
   regime_bias + causal_direction + momentum_sign = direction
   All agree → HIGH conviction. Mixed → LOW conviction.

Present: "EXECUTE LONG AAPL" or "EXECUTE SHORT AAPL" or "EXECUTE — direction unclear, reduce size"
```

### Gap 3: Position Sizing from F

```
F_free_energy from cockpit → conviction = 1.0 - F_free_energy
conviction = 0.9 (F very low) → 90% of base size
conviction = 0.5 (F moderate) → 50% of base size
conviction = 0.2 (F high) → 20% of base size (barely EXECUTE)

base_size = buying_power × risk_percent (default 2%)
adjusted = base_size × conviction × (1.0 - self_impact_ratio)

Example: $50,000 buying power × 2% = $1,000 base
  conviction=0.9, self_impact=0.02 → $1,000 × 0.9 × 0.98 = $882
```

### Gap 4: Stop-Loss from Resonance Width

```
After entry at price P:
  Read atom.center and atom.spread from cockpit
  stop_distance = spread × price / center (scaled to price units)
  For LONG:  stop = entry_price - stop_distance
  For SHORT: stop = entry_price + stop_distance

When submitting to Alpaca:
  alpaca submit_order ... type=limit limit_price=P
  alpaca submit_order ... type=stop stop_price=STOP (protective)
```

The stop IS the edge of the resonance band. Outside it = anomaly = exit.

### Gap 5: Multi-Timeframe Consensus

`/focus TICKER` feeds 7 layers. After all layers absorbed, SYNTHESIZE:

```
For each timeframe substrate, check cockpit:
  macro  (${TICKER}_macro):   direction from gradient slope
  vol    (${TICKER}_tech):    direction from RSI/MACD
  flow   (${TICKER}_flow):    direction from insider net
  tick   (${TICKER}):         direction from recent deltas
  book   (${TICKER}_book):    direction from bid/ask imbalance

Consensus = Σ(direction_i × weight_i)
  Weights: macro=0.30, vol=0.25, flow=0.20, tick=0.15, book=0.10

|consensus| > 0.5 → strong signal → full size
|consensus| 0.2-0.5 → moderate → half size
|consensus| < 0.2 → weak → skip or minimum size
sign(consensus) = direction (positive=LONG, negative=SHORT)
```

### Gap 6: Auto-Tune Feedback Loop

**MANDATORY** after every `/act`:

```
1. Wait for Alpaca fill confirmation
2. Feed result: csos substrate=${TICKER}_exec output="side:BUY filled:182.01 slip:0.01 impact:0.02"
3. Track P&L: csos substrate=${TICKER}_pnl output="entry:182.01 current:183.50 pnl:+0.82%"
4. Feed P&L updates periodically (every 30min or on significant move)

When closing position:
5. Feed final: csos substrate=${TICKER}_pnl output="entry:182.01 exit:185.20 pnl:+1.75% duration:2d"
6. This trains the causal chain:
   - CausalAtom that predicted this trade gets counterfactual_score updated
   - Profitable → strength increases → future similar patterns get more weight
   - Unprofitable → strength decreases → pattern may be pruned

Over time: the system AUTO-TUNES:
  - Profitable causal chains strengthen → more decisive → faster EXECUTE
  - Unprofitable chains weaken → more cautious → more EXPLORE
  - Edge decay from adversarial atoms → abandons crowded strategies
  - Regime HMM updates → correct bias for current market state
```

## Fundamental Encoding — The 5 Forces of Stock Worth

Every stock is a system with 5 observable forces. Each force encodes as a signed substrate.

### Force 1: WORTH (Is the company growing?)
```
financialdatasets getIncomeStatements (4 quarters) → revenue Q/Q trend, EPS trend, margin trend
→ csos substrate=${T}_worth output="rev_growth:+12% eps_growth:+8% margin:+2%"

Encoding: all rising → direction:+1 (growth). Mixed → 0. All falling → -1 (decay).
```

### Force 2: HEALTH (Can it sustain?)
```
financialdatasets getBalanceSheets → debt_to_equity, current_ratio, cash position
→ csos substrate=${T}_health output="debt_equity:1.2 current_ratio:2.1 cash:5.2B"

Encoding: D/E < 1.5 AND current > 1.5 → direction:+1. D/E > 2.0 OR current < 1.0 → -1.
```

### Force 3: TRAFFIC (Is attention growing?)
```
Volume now vs 30-day average → vol_ratio
→ csos substrate=${T}_traffic output="vol_ratio:1.4 direction:+1"

Encoding: ratio > 1.3 → surging (+1). 0.7-1.3 → normal (0). < 0.7 → fading (-1).
```

### Force 4: POTENTIAL (What can it capture?)
```
Revenue run-rate vs sector peers, margin vs sector average
→ csos substrate=${T}_potential output="run_rate:48B moat:strong"

Encoding: margin > sector AND growing → +1 (strong moat). Margin declining toward sector → -1 (eroding).
```

### Force 5: CATALYST (What's driving direction?)
```
Insider trades + news sentiment + fundamental trend alignment
→ csos substrate=${T}_catalyst output="insider_net:+9 news:+0.6 hype:false org_decay:false"

HYPE detection: news positive BUT worth declining → hype = true → bearish override.
ORG DECAY detection: margins falling + insiders selling + debt rising → org_decay = true → exit signal.
```

### Bullish vs Bearish Decomposition

| Pattern | Signal | Action |
|---------|--------|--------|
| Worth↑ Health↑ Traffic↑ | Genuine growth | LONG with conviction |
| Worth↑ Health↑ Traffic↓ | Underappreciated | LONG (contrarian, smaller size) |
| Worth↓ Health↓ Traffic↑ | Hype / speculation | AVOID or SHORT |
| Worth↓ Health↓ Traffic↓ | Organizational decay | SHORT or EXIT |
| Worth↑ Health↓ Traffic↑ | Growth burning cash | WATCH (needs catalyst clarity) |

## Complete Trading Workflow

```
/skill econophysics           ← load this skill
/feed "the market"            ← macro: SPY + VIX + DXY (via eodhd)
/check everything             ← organism decision, regime, motor
/focus AAPL                   ← 10-layer deep dive (structural + momentum + tactical)
/check AAPL                   ← decision=EXECUTE? What direction?

If EXECUTE:
  Synthesize direction (regime + causal + momentum + worth + health)
  Compute size (F → conviction → adjusted size)
  Compute stop (resonance width → stop distance)
  /act AAPL                   ← present thesis, confirm, execute

After fill:
  Feed result back (slip, impact)
  Monitor P&L via periodic /feed
  When Boyer flips to EXPLORE → close position
  Feed close result → auto-tune loop completes

Monthly:
  /assess AAPL                ← health card: worth, health, traffic, potential, catalyst
  If DETERIORATING → review position for exit
  If HEALTHY + not held → candidate for /focus deep dive
```

## MCP Feed Patterns (with directional signals)

```
eodhd real-time AAPL.US → close=182.50, change=+1.50, vol=45M
→ csos substrate=AAPL output="price:182.50 change:+1.50 pct:+0.83 vol:45M"

eodhd VIX.INDX → close=19.23
→ csos substrate=vix output="level:19.23 change:-0.5 regime:moderate"

eodhd UUP.US → close=27.44
→ csos substrate=dxy output="level:27.44 change:-0.15"

financialdatasets getIncomeStatements AAPL (4Q) → rev_growth=+12%, eps_growth=+8%
→ csos substrate=AAPL_worth output="rev_growth:+12 eps_growth:+8 margin:+2"

financialdatasets getBalanceSheets AAPL → D/E=1.2, current=1.8
→ csos substrate=AAPL_health output="debt_equity:1.2 current_ratio:1.8 cash:29B"

financialdatasets getInsiderTrades AAPL → buys=12 sells=3
→ csos substrate=AAPL_flow output="buys:12 sells:3 net:+9 direction:+1"

alpaca get_latest_quote AAPL → bid=182.48 ask=182.52
→ csos substrate=AAPL_book output="bid:182.48 ask:182.52 imbalance:+0.30"
```

**Every signal includes a directional component.** Sign matters.

## Absolute Limits

| Limit | Value | Source |
|-------|-------|--------|
| Observation noise | ~1bp | Heisenberg |
| Model capacity | 32 atoms | Kolmogorov |
| Self-reference | unavoidable | Gödel |
| Computation | ~10⁻¹⁵ J/absorb | Landauer |
| η_max | 95% | Same as photosynthesis |
