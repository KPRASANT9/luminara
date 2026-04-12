---
name: playbook
description: "Auto-pilot trading. Activate → detect → run → pause only at trade gates."
---

# Playbook — Autonomous Protocol

On activation: `/skill econophysics` loads silently. Then detect context and go.

## STEP 0: DETECT (automatic)

```
csos action=greenhouse → existing sessions?
csos action=muscle → motor memory?
csos ring=eco_organism detail=cockpit → organism state?
```

| Context | Flow |
|---------|------|
| Cold start / no sessions | → Flow A (Morning) |
| Sessions with EXECUTE | → Flow B (Opportunity) |
| All EXPLORE, positions open | → Flow C (Monitor) |
| VIX extreme / F rising everywhere | → Flow D (Crisis) |

## Flow A: Morning Discovery

```
1. /globe                    ← 26 substrates, 3 batches, automatic
2. Read per-ring regime + dip/peak stage
3. Report planetary dashboard
4. If EXECUTE on any substrate → Flow B
5. If crisis signals → Flow D
```

## Flow B: Opportunity Pipeline

For each EXECUTE substrate:
```
1. /assess {sub}             ← health card
   DETERIORATING → skip, report why
   HEALTHY → continue ↓
2. /focus {sub}              ← 10-layer + earnings check
   WEAK consensus → skip
   HYPE detected → skip
   STRONG/MODERATE → continue ↓
3. ★ PRESENT THESIS (human pause):
   DIRECTION | CONSENSUS | SIZE | STOP | CONVICTION
   TRAPS: HYPE/ORG_DECAY/NONE
   → YES / NO / ADJUST
4. On YES → /act {sub} → alpaca orders → feed result back
```

## Flow C: Monitor

```
1. /feed each active session with latest data
2. /check each held position
   Flipped to EXPLORE? → flag exit: CLOSE / HOLD
3. /check scale (weekly) → diminishing returns?
   Adapt: switch feed sources, re-run /rotate
```

## Flow D: Crisis

```
1. /feed vix + dxy → /check everything
2. Triage positions: HOLD / TIGHTEN / EXIT
3. ★ PRESENT CRISIS CARD (human pause):
   → EXECUTE ALL / HOLD ALL / specify
```

## Flow E: Planetary Scan

Triggers on: "globe", "world", "overnight", "what happened?"
```
1. /globe → 26 substrates, 4 rings
2. Detect 6 timezone gaps (latency, coupling flip, regime desync, FX, liquidity, earnings season)
3. Report dashboard
4. Leadership FLIP → /rotate. Dip stage 5-6 → /focus. Crisis → Flow D.
```

## Auto-Pilot Rules

1. No user input needed for: data fetching, absorption, cockpit reads, greenhouse checks
2. Human pauses: trade execution (YES/NO), exit signal (CLOSE/HOLD), crisis actions
3. Always use verified ticker table — never search/resolve
4. Always feed signed deltas — never price-only
5. Every report ends with next action, not "what would you like?"
6. Feed results back after every trade (auto-tune is non-negotiable)

## Autonomous Cadence (IST)

| Time | Action |
|------|--------|
| 05:30 | Feed Asia open + overnight bridges |
| 09:15 | Feed India open + Asia mid-session |
| 12:30 | Feed Europe open + India mid-session |
| 15:30 | Feed India close → predict US |
| 19:00 | Feed US open (London-NYSE overlap = max coupling) |
| 20:30 | ★ /auto full cycle — all rings processed, proposals generated |
| 01:30 | Feed US close + bridges through dark window |

## Quick Reference

```
"What's the market doing?"    → /scan
"Show the planet"             → /globe
"Deep dive AAPL"              → /focus AAPL
"Is AAPL healthy?"            → /assess AAPL
"Buy AAPL"                    → /act AAPL
"Run everything"              → /auto
"Is the system still working?" → /check scale
```
