---
name: workflow
description: "Pipeline synthesis, pre-built recipes, and autonomous workflow execution."
---

# Workflows — Pre-Built Pipelines for Autonomous Operation

The workflow skill provides ready-to-run pipelines that compose the 7 commands
(feed, check, act, scan, focus, rotate, globe) into autonomous sequences.
Each pipeline is a recipe the agent follows. No new C code — the commands ARE the engine.

## Three Laws of Workflow

1. **Physics decides.** Every pipeline runs Feed → Check → Act. Never skip Check.
2. **Human gates trades.** Pipelines pause at PROPOSE. Everything else is autonomous.
3. **Feedback is mandatory.** Every execution feeds results back. No fire-and-forget.

---

## Pre-Built Pipelines

### `morning_scan` — Daily 10-minute regime check

**When:** Every morning before market open. IST 08:30 recommended.
**Layers:** L3-L5 (macro + timezone + planet)
**Autonomous:** Fully — no human gate until trade proposal.

```
STEPS:
  1. /globe                        ← Feed all 26 substrates (3 batches)
  2. csos ring=eco_organism detail=cockpit  ← Organism decision
  3. csos action=greenhouse        ← Convergences + coupling map
  4. csos action=muscle            ← Motor memory priorities
  5. REPORT: regime + top sectors + convergences + overnight gaps
  6. IF EXECUTE on any substrate → branch to opportunity_pipeline
  7. IF crisis signals (VIX spike, coupling density) → branch to crisis_pipeline
```

**Output:** Planetary dashboard with regime, ring scores, and next-action recommendation.

---

### `opportunity_pipeline` — Single instrument deep dive + trade

**When:** After morning_scan detects EXECUTE, or user says "should I buy X?"
**Layers:** L1-L3 (instrument + sector + macro)
**Autonomous:** Until PROPOSE gate.

```
STEPS:
  1. /focus {SUBSTRATE}            ← 7-10 layer deep dive
  2. /assess {SUBSTRATE}           ← 5-force health card
  3. CHECK: all 5 L1 conditions met?
     - Gradient 2x sector average?
     - Motor strength > 0.7?
     - CausalAtom strength > 0.8?
     - Insider flow directional?
     - Multi-layer alignment?
  4. IF < 3 conditions → FALLBACK to sector ETF (L2)
  5. IF >= 3 conditions → BUILD THESIS:
     direction  = regime + causal + momentum synthesis
     size       = base × (1 - F)
     stop       = resonance_width → stop distance
     conviction = avg(1 - F) across aligned layers
  6. ★ PROPOSE: present thesis (HUMAN GATE)
     → GO / SKIP / ADJUST / WAIT
  7. ON GO: /act {SUBSTRATE} → alpaca orders → feed result back
  8. FEEDBACK: csos substrate={SUB}_exec output="fill:P slip:S"
```

---

### `weekly_rotate` — Sector rotation rebalancing

**When:** Weekly (Sunday evening or Monday pre-market).
**Layers:** L2-L3 (sectors + macro forces)
**Autonomous:** Until PROPOSE gate.

```
STEPS:
  1. /skill econophysics           ← Load direction + sizing logic
  2. /rotate                       ← Full 7-step cycle:
     a. Feed 6 macro forces + 11 sector ETFs
     b. Rank: gradient × direction × (1 - F)
     c. Allocate: top 3 LONG, bottom 3 SHORT, middle 5 SKIP
     d. Duration: from HMM persistence probability
  3. ★ PROPOSE: OVERWEIGHT/UNDERWEIGHT table (HUMAN GATE)
  4. ON GO: /act each sector → alpaca orders → feed results
  5. FEEDBACK: feed all execution results back
  6. MONITOR: daily /scan watches for 3+ regime change signals → RE-ROTATE
```

---

### `monthly_globe` — Planetary allocation review

**When:** Monthly (first weekend).
**Layers:** L4-L5 (timezone rings + planet)
**Autonomous:** Until PROPOSE gate.

```
STEPS:
  1. /globe                        ← 26 substrates, 4 rings
  2. Detect dip/peak stage per ring (6-stage sequence)
  3. Score: ring_gradient × regime_mult × currency_adj × liquidity_weight
  4. ALLOCATE across geographies (F-weighted)
  5. ★ PROPOSE: planetary allocation table (HUMAN GATE)
  6. ON GO: execute via regional ETFs → feed results
  7. MONITOR: daily /scan for regime shift across rings
```

---

### `continuous_monitor` — Position monitoring loop

**When:** Continuously while positions are open.
**Layers:** L1-L2 (instruments + sectors)
**Autonomous:** Fully — alerts on exit signals.

```
STEPS (loop):
  1. FOR each open position:
     a. /feed {TICKER} with latest price
     b. csos ring={TICKER} detail=cockpit
     c. CHECK: still EXECUTE?
        - YES → continue holding
        - NO (flipped to EXPLORE) → FLAG EXIT
        - F rising for 3+ cycles → FLAG DETERIORATING
  2. IF EXIT flagged:
     ★ PROPOSE: CLOSE / HOLD / TIGHTEN (HUMAN GATE)
  3. IF all EXPLORE + no flags → sleep until next feed cycle
```

---

### `cold_start` — Bootstrap a new substrate from zero

**When:** User wants to track a new ticker/domain.
**Layers:** All
**Autonomous:** Fully.

```
STEPS:
  1. /skill bridge                 ← Load cold-start protocol
  2. IDENTIFY: ticker → MCP source → verified ticker mapping
  3. FEED: 50 initial signals (historical + current)
  4. CHECK: csos ring={SUB} detail=cockpit
     - Motor strength? (needs > 0.3 for meaningful decisions)
     - Atom count? (Calvin atoms forming?)
     - F trajectory? (decreasing = learning)
  5. REPORT: readiness score + recommended next action
  6. IF ready → available for opportunity_pipeline
  7. IF not ready → schedule 50 more feeds
```

---

### `crisis_pipeline` — Emergency triage

**When:** VIX spike, coupling density surge, or regime HMM flips to CRISIS.
**Layers:** All
**Autonomous:** Until PROPOSE gate.

```
STEPS:
  1. /feed vix + dxy + bonds       ← Crisis indicators
  2. /check everything             ← Full organism cockpit
  3. TRIAGE each open position:
     - HOLD: F stable, regime aligned
     - TIGHTEN: F rising, stop → resonance_width × 0.5
     - EXIT: F spiking, regime flipped
  4. ★ PROPOSE: crisis card (HUMAN GATE)
     → EXECUTE ALL / HOLD ALL / per-position
  5. ON EXECUTE: close/tighten via alpaca → feed results
  6. MONITOR: hourly /scan until crisis regime exits
```

---

### `auto_tune` — Performance review + adaptation

**When:** After every trade closes, or quarterly review.
**Layers:** All
**Autonomous:** Fully.

```
STEPS:
  1. GATHER: all closed position P&L from motor memory
  2. SCORE: CausalAtoms via dark_counterfactual
     - Profitable → chain strengthens (natural selection)
     - Unprofitable → chain weakens → eventually pruned
  3. DETECT: diminishing returns
     - Edge decay rate accelerating? (adversarial crowding)
     - Same patterns winning less? (strategy saturation)
     - New patterns emerging? (regime shift opportunity)
  4. ADAPT:
     - Switch feed sources for stale substrates
     - Increase weight on confirmed timezone lags
     - Reduce conviction on crowded patterns
     - Flag regime shifts for /rotate re-run
  5. REPORT: adaptation summary + F trajectory
```

---

## Composing Custom Pipelines

Pipelines are just sequences of commands with conditions. Compose them:

```
# Example: "Feed India, check if EXECUTE, then focus top sector"
pipeline india_opportunity:
  1. /feed nifty50
  2. /feed banknifty
  3. /check nifty50 → IF EXECUTE → /focus nifty50
  4. /check banknifty → IF EXECUTE → /focus banknifty
  5. BEST = highest gradient between nifty50 and banknifty
  6. /assess {BEST}
  7. ★ PROPOSE
```

## IST Cadence Integration

These pipelines map to the auto-feed cron schedule:

| IST Time | Cron Phase | Pipeline |
|----------|-----------|----------|
| 05:30 | asia_open | auto-feed → morning_scan (partial) |
| 09:15 | india_open | auto-feed → continuous_monitor |
| 13:30 | europe_open | auto-feed → continuous_monitor |
| 15:30 | india_close | auto-feed → continuous_monitor |
| 19:00 | us_open | auto-feed → continuous_monitor |
| 20:30 | golden_hour | auto-feed → morning_scan (full) + opportunity check |
| 01:30 | us_close | auto-feed → save state |
| Weekly | manual | weekly_rotate |
| Monthly | manual | monthly_globe |
| On trade close | automatic | auto_tune |

## Quick Reference

```
"What's the market doing?"      → morning_scan
"Should I buy AAPL?"            → opportunity_pipeline AAPL
"Rebalance my portfolio"        → weekly_rotate
"Show the planet"               → monthly_globe
"Check my positions"            → continuous_monitor
"Add a new ticker"              → cold_start {TICKER}
"Market is crashing"            → crisis_pipeline
"How am I doing?"               → auto_tune
"Run everything"                → /auto (combines all pipelines)
```
