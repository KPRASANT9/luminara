---
name: bridge
description: How the native membrane shares state across agents and sessions. Single binary, single process, 3 compartments.
---

# Agent Bridge — Native Membrane

All agents share ONE `./csos` process → ONE organism → THREE compartments (eco_domain, eco_cockpit, eco_organism). State persists to `.csos/rings/*.json` between sessions.

## What Carries Across Sessions

| State | Storage | Loaded By |
|-------|---------|-----------|
| Gradient (evidence count) | `.csos/rings/eco_*.json` | `store.c` at binary startup |
| Atom parameters (tuned by NPQ) | `.csos/rings/eco_*.mem.json` | `spec_parse.c` + `store.c` |
| Motor memory (spaced repetition) | Embedded in membrane state | `store.c` restore |
| Calvin atoms (learned patterns) | `.csos/rings/eco_*.mem.json` | `spec_parse.c` Calvin loader |
| Human data + skills | `.csos/sessions/human.json` | `protocol.c` remember/recall |
| Agent mode (plan/build) | Membrane `mode` field | Persisted in ring state |
| Boyer decision | Membrane `decision` field | Recomputed on next absorb |

## How Agents Share State

```
@csos-living (session 1)
    │
    ├── csos-core absorb → eco_domain gradient grows
    ├── csos-core remember key=X value=Y → human.json
    ├── Calvin synthesizes new atom → eco_domain.mem.json
    │
    └── Binary exits → store.c saves to .csos/rings/

@plan (session 2)
    │
    ├── Binary starts → store.c loads from .csos/rings/
    ├── Gradient, motor memory, Calvin atoms all restored
    ├── Boyer gate fires on accumulated evidence from session 1
    │
    └── @csos-living transitions to @build (physics decided)
```

## Physics Constants That Shape Behavior

| Constant | Value | What It Controls |
|----------|-------|-----------------|
| `CSOS_MOTOR_DECAY` | 0.99 | Substrate priority fades 1% per cycle |
| `CSOS_MOTOR_GROWTH` | 0.1 | Spaced encounters strengthen memory |
| `CSOS_BOYER_THRESHOLD` | 0.3 | action_ratio > 0.3 triggers EXPLORE |
| `CSOS_STUCK_CYCLES` | 2 | 2 zero-delta cycles → switch mode |
| `CSOS_CALVIN_FREQUENCY` | 5 | New pattern synthesis every 5 cycles |

All constants defined in `lib/membrane.h`, derived from the 5 equations.
