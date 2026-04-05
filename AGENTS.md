# CSOS — Operate, Guide, Deliver

> ALL data and output flows through csos-core. No other tool writes files, fetches URLs, or runs commands.

## The One Tool

csos-core is the ONLY tool for external I/O. Everything else is denied or read-only.

| Tool | Access | Purpose |
|------|--------|---------|
| **csos-core** | ALLOW | All 20 actions. Physics-validated. Auto-absorb. Law-enforced. |
| **read/glob/grep** | ALLOW | Read-only. MUST absorb results via `csos-core substrate=X output="..."` |
| **webautomate** | ALLOW | Browser login only (cookie capture) |
| **skill** | ALLOW | OpenCode skill invocation |
| **write/edit** | DENY | Use `csos-core content="..."` or `csos-core channel=...` |
| **webfetch/websearch** | DENY | Use `csos-core url="..."` (auto-absorbs) |
| **bash** | DENY | Use `csos-core command="..."` (auto-absorbs + law enforcement) |

## Why Tool-Only

Every csos-core operation is auto-absorbed, physics-validated, motor-tracked, law-enforced, and egress-routed. Operations through other tools bypass all of this.

## The Guidance Loop

```
HIGH ENTROPY     "Help me land a job"              (infinite possibilities)
      | observe via csos-core -> physics says EXPLORE
MEDIUM           "700 developer jobs found"         (still too broad)
      | gradient stagnates -> ask ONE question
      | human: "Senior Android, 180-220k"
NARROWING        "23 matching roles"                (focused)
      | observe via csos-core -> physics says EXECUTE
MINIMUM ENTROPY  "Top 5 roles. Apply?"              (actionable)
      | deliver via csos-core content="..."
```

## How Agents Read the Physics

Every csos-core response contains the decision. No separate cockpit call needed.

```json
{"decision":"EXECUTE", "delta":5, "motor_strength":0.82, "mode":"build"}
```

| Reading | Meaning | Agent Does |
|---------|---------|-----------|
| decision = EXECUTE | Enough evidence | `csos-core content="..."` to deliver |
| delta > 0, action_ratio > 0.3 | Tools yielding | Observe more via csos-core |
| delta = 0, action_ratio < 0.3 | Tools exhausted | Ask human ONE question |
| speed > rw | High confidence | Deliver even if ratio low |

## 3 Agents, 1 Entry Point

| Agent | Mode | Purpose |
|-------|------|---------|
| **@csos-living** | primary (default) | Everything. Transitions plan/build via Boyer. |
| @plan | internal | Observation only |
| @build | internal | Delivery only (via csos-core) |

The human never chooses. @csos-living handles it.

## 3 Laws

**I. No Hardcoded Logic.** bash/write/edit denied. csos-core command= checks forbidden patterns.

**II. All State from .csos/.** Membrane state (.mem.json), human data (human.json), egress config (egress.json).

**III. New Substrates = Zero Code.** `csos-core command="new-tool" substrate=new_thing` — Calvin adapts.

## 5 Equations

| # | Equation | Role |
|---|----------|------|
| 1 | Gouterman | Does this signal match known patterns? |
| 2 | Marcus | How far off is reality from prediction? |
| 3 | Mitchell | How much evidence accumulated? |
| 4 | Forster | Can knowledge transfer between substrates? |
| 5 | Boyer | Is evidence sufficient for a decision? |

## Structure

```
lib/membrane.h              Unified photon + atom + membrane types
src/native/membrane.c       THE process: membrane_absorb()
src/native/protocol.c       20 actions, all protocols
src/native/csos.c           Entry point + stress tests
src/native/jit.c            LLVM JIT for membrane_absorb
src/core/core.py            Python reference (fallback)
.opencode/tools/csos-core.ts   THE tool (spawns ./csos binary)
.opencode/agents/csos-living.md   THE agent (nervous system)
.csos/rings/*.mem.json      Persisted membrane state
.csos/sessions/human.json   Human answers + skills
.csos/egress.json           Delivery channel routing
```
