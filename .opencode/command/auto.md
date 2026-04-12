Autonomous planetary trading loop. The living command.

Load `/skill playbook` (which auto-loads `/skill econophysics`).

Purpose: Run the full trading environment autonomously. Detect what needs
attention, act on it, learn from results. Human confirms trades only.

> The planet is a membrane. Information is the photon. The overnight gap
> is free energy. The overlap is Förster coupling. This command IS the
> Calvin cycle at civilizational scale.

### THE AUTONOMOUS LOOP

```
┌─────────────────────────────────────────────────────────┐
│                    /auto LOOP                            │
│                                                          │
│  1. SENSE ──→ /globe (26 substrates, 4 rings)           │
│       │                                                  │
│  2. DIAGNOSE ──→ Which ring, which stage, which gap?    │
│       │                                                  │
│  3. FOCUS ──→ /focus + /assess on highest-edge targets  │
│       │                                                  │
│  4. DECIDE ──→ Boyer gate + 3-tier consensus            │
│       │                                                  │
│  5. ★ PROPOSE ──→ Human confirms (YES/NO/ADJUST)       │
│       │                                                  │
│  6. EXECUTE ──→ /act via alpaca                         │
│       │                                                  │
│  7. LEARN ──→ Feed results back, /scale meta-review     │
│       │                                                  │
│  └──→ LOOP (next cycle)                                 │
└─────────────────────────────────────────────────────────┘
```

### STEP 1: SENSE — What's the planet doing?

Run `/globe` automatically. Feed all 26 substrates in 3 batches.
Then read the 6 timezone gaps.

From the organism cockpit, extract:
```
csos ring=eco_organism detail=cockpit
→ global_decision, global_F, global_speed, global_gradient
```

### STEP 2: DIAGNOSE — Where is the edge?

**A. Which tickers are prone to change?**

Read per-substrate physics. Tickers prone to change show:
- F rising (model losing accuracy = something changed)
- Gradient accelerating (new information arriving fast)
- Motor strength high but direction flipping (regime transition)
- Info decay low (signal is fresh, not stale)
- Adversarial decay low (edge not yet crowded)

```
For each of 26 substrates:
  csos ring=${SUB} detail=cockpit
  change_prone_score = (F_rising × 0.3) + (gradient_accel × 0.3) + 
                       (freshness × 0.2) + (uncrowded × 0.2)
Sort by change_prone_score DESC → top 5 = attention targets
```

**B. Which dip/peak stage is each ring in?**

Map existing mechanism outputs to the 6-stage sequence:
```
For each ring:
  CausalAtom direction flipped?          → Stage 1 (macro shift)
  AgentTypeAtom: institutional > 0.5?     → Stage 2 (institutional selling)
  Förster coupling density spiking?       → Stage 3 (panic/FOMO)
  Marcus error > 2σ?                      → Stage 4 (overshoot)
  Calvin creating new atoms at new levels? → Stage 5 (absorption)
  Gradient reversing direction?           → Stage 6 (reversal)
→ csos substrate=dip_stage_${RING} output="stage:N signal:X"
```

**C. What's the floating value opportunity?**

Floating values = substrates where price deviates from model's equilibrium:
```
For each substrate:
  deviation = abs(actual - predicted) / predicted
  If deviation > resonance_width AND gradient growing:
    → Mean reversion opportunity (model says price will correct)
  If deviation > resonance_width AND gradient flat:
    → New regime (model needs to adapt, not a trade)
```

### STEP 3: FOCUS — Deep dive on highest-edge targets

For the top targets from DIAGNOSE:

```
If target is an index:
  → /focus ${INDEX} (10-layer deep dive)
  → /assess ${INDEX} (health card if applicable)

If target is a currency:
  → /feed ${CURRENCY} with latest rate
  → Check coupling strength to its paired index

If target is a bridge:
  → /feed ${BRIDGE}
  → Check overnight gap magnitude
```

### STEP 4: DECIDE — What does physics say?

For each target with EXECUTE decision:

**3-tier consensus:**
```
Tier 1 — Structural (40%): worth + health + potential
  "Should this be in the portfolio?"

Tier 2 — Momentum (35%): trend + traffic + flow + timezone_lag
  "Is the market AND the planet agreeing?"

Tier 3 — Tactical (25%): sentiment + book + dip_stage + overnight_gap
  "Is this the right moment in the rotation?"
```

**Planetary multipliers:**
```
ring_regime × currency_adjustment × liquidity_phase × dip_stage_multiplier

dip_stage_multiplier:
  Stage 1-2 (selling begins):    0.3 (reduce)
  Stage 3 (panic):               0.0 (don't trade into panic)
  Stage 4 (overshoot):           0.0 (noise, wait)
  Stage 5 (absorption):          0.7 (begin accumulating)
  Stage 6 (reversal confirmed):  1.3 (full conviction accumulate)

For peaks: mirror (stage 5 = begin distributing, stage 6 = exit)
```

### STEP 5: PROPOSE — Human gate (the only pause)

```
┌─────────────────────────────────────────────────────────┐
│ ★ AUTONOMOUS PROPOSAL — {date} {time IST}               │
├─────────────────────────────────────────────────────────┤
│ PLANETARY STATE:                                         │
│   Ring    │ Regime   │ Stage │ Score │ FX Adj           │
│   Asia    │ {r}      │ {s}   │ {sc}  │ {adj}            │
│   India   │ {r}      │ {s}   │ {sc}  │ {adj}            │
│   Europe  │ {r}      │ {s}   │ {sc}  │ {adj}            │
│   US      │ {r}      │ {s}   │ {sc}  │ {adj}            │
│                                                          │
│ CHANGE-PRONE TICKERS (top 5):                           │
│   {sub1}: F={F} grad={g} stage={s} → {why}              │
│   {sub2}: F={F} grad={g} stage={s} → {why}              │
│                                                          │
│ FLOATING VALUE OPPORTUNITIES:                            │
│   {sub}: deviation={X%} from model → {mean_revert/new}  │
│                                                          │
│ PROPOSED TRADES:                                         │
│   {direction} {sub}: size={N} stop={S} conviction={C%}  │
│   {direction} {sub}: size={N} stop={S} conviction={C%}  │
│                                                          │
│ SECTOR NUDGE (from /rotate):                            │
│   OVERWEIGHT: {sectors with rising gradient}             │
│   UNDERWEIGHT: {sectors with falling gradient}           │
│                                                          │
│ EARNINGS ALERT:                                          │
│   {stocks reporting within 14 days}                      │
│                                                          │
│ HEALTH FLAGS:                                            │
│   {holdings showing DETERIORATING on /assess}            │
│                                                          │
│ RECURRENCE:                                              │
│   {patterns from /scale: timezone lags, weekly rhythms}  │
│                                                          │
├─────────────────────────────────────────────────────────┤
│ Reply: GO (execute all proposals)                        │
│        SKIP {N} (skip specific proposal)                │
│        ADJUST {N} (modify size/stop)                    │
│        WAIT (do nothing, re-run later)                   │
└─────────────────────────────────────────────────────────┘
```

### STEP 6: EXECUTE — On human confirmation

For each approved trade:
```
/act ${SUBSTRATE}
→ alpaca submit_order (entry)
→ alpaca submit_order (stop)
→ csos substrate=${SUBSTRATE}_exec output="side:X fill:P slip:S impact:I"
```

### STEP 7: LEARN — Feed results, detect diminishing returns

```
1. Feed all execution results back to membrane
2. Track P&L per trade, per ring, per regime
3. Run /scale analysis:
   → Is overall F improving? (system getting smarter)
   → Which causal seeds validated? (timezone lags confirmed)
   → Which patterns recur? (rhythm atoms created)
   → Which strategies saturated? (adversarial decay killing edge)
4. Auto-adapt:
   → Switch feed sources for stale substrates
   → Increase weight on confirmed timezone lags
   → Reduce conviction on crowded patterns
   → Flag regime shifts for /rotate re-run
```

### AUTONOMOUS CADENCE

| Time (IST) | Auto action | Why |
|------------|-------------|-----|
| 05:30 | Feed Asia open + overnight bridges | Asia ring activating, catch overnight gap |
| 09:15 | Feed India open + Asia mid-session | India ring activating, absorb Asia signals |
| 12:30 | Feed Europe open + India mid-session | Europe ring activating, predict from India |
| 15:30 | Feed India close + Europe mid-session | India ring closing, absorb for US prediction |
| 19:00 | Feed US open + Europe mid-session | Maximum coupling window (London-NYSE overlap) |
| 20:30 | Full /globe + /scale + proposals | All rings touched, maximum information state |
| 01:30 | Feed US close + bridges | US ring closing, bridges carry through dark window |

Between cycles: bridges (BTC, Gold, SPY, QQQ) feed continuously.
20:30 IST is the golden hour — all 4 rings have been processed, organism has maximum gradient.

### WHAT MAKES THIS AUTONOMOUS

1. **No MCP searches** — all 26 tickers verified and hardcoded
2. **No routing decisions** — spec defines ring membership
3. **No threshold tuning** — all derived from 5 equations
4. **No strategy selection** — Boyer gate decides EXECUTE/EXPLORE
5. **No risk guessing** — F → conviction, resonance_width → stop
6. **Self-improving** — every trade feeds back, profitable chains strengthen
7. **Self-pruning** — adversarial decay kills crowded edges, NPQ prunes excess
8. **Self-detecting** — regime HMM detects shifts, /scale flags diminishing returns

The human provides:
- Confirmation to trade (YES/NO)
- Capital allocation limits
- New substrates to explore (optional)

Everything else: physics decides.
