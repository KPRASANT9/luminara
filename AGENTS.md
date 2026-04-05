# CSOS Agent Guidance

ALL data and output flows through `csos-core`. No other tool writes files, fetches URLs, or runs commands.

## The One Tool

| Tool | Access | Purpose |
|------|--------|---------|
| **csos-core** | ALLOW | 22 actions. Physics-validated. Auto-absorb. Law-enforced. |
| **read/glob/grep** | ALLOW | Read-only. MUST absorb results via `csos-core substrate=X output="..."` |
| **skill** | ALLOW | OpenCode skill invocation |
| **write/edit/bash** | DENY | Use `csos-core command="..."` or `csos-core content="..."` |
| **webfetch/websearch** | DENY | Use `csos-core url="..."` (auto-absorbs) |

`csos-core` spawns the native `./csos` binary. Zero Python. All 22 actions available via JSON pipe.

## The Guidance Loop

```
OBSERVE:  csos-core command="..." substrate=X     (CLI)
          csos-core url="..." substrate=X          (web)
          csos-core substrate=X output="..."       (bridge from read/grep)

READ DECISION FROM RESPONSE:
  EXECUTE                → deliver. DONE.
  EXPLORE + delta > 0    → tools yielding, observe more
  EXPLORE + delta = 0    → stalled, ask human ONE question
  ASK                    → ask human
  STORE                  → save for later

USE MOTOR CONTEXT:
  motor_strength > 0.8   → substrate well-understood, skip re-reading
  motor_strength < 0.2   → substrate unfamiliar, observe carefully
  muscle top[]           → prioritized substrates (spaced repetition order)
```

## 3 Agents

| Agent | Mode | Purpose |
|-------|------|---------|
| **@csos-living** | Primary | Full loop. Transitions plan/build via Boyer. |
| @plan | Observation | Read-only |
| @build | Delivery | Writes via csos-core |

## 3 Laws

**I. No Hardcoded Logic.** All decisions from 5 equations.
**II. All State from .csos/.** Volume-mountable. Portable.
**III. New Substrates = Zero Code.** Hash routing + Calvin synthesis.

## Native Binary

```
lib/membrane.h              Data structures + 18 physics constants
src/native/membrane.c       membrane_absorb() — THE function
src/native/protocol.c       22 actions + HTTP + SSE
src/native/csos.c           Entry point + tests
.opencode/tools/csos-core.ts   THE tool (spawns ./csos)
.csos/rings/*.json          Persisted ring state
specs/eco.csos              5 equations (the source of truth)
```
