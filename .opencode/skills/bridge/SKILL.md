---
name: bridge
description: How the unified membrane shares state across agents and sessions.
---

# Agent Bridge — Unified Membrane

All agents share ONE daemon process → ONE organism → THREE membranes (eco_domain, eco_cockpit, eco_organism). State accumulates within a session and persists to `.csos/rings/*.mem.json` between sessions.

| What Carries Across | How |
|---|---|
| Gradient (evidence count) | Membrane state persisted on exit, loaded on start |
| Motor memory (substrate priorities) | Embedded in membrane, persisted as motor[] array |
| Calvin atoms (learned patterns) | Persisted as calvin_atoms[] in .mem.json |
| Human answers + skills | Stored in .csos/sessions/human.json |
| Agent mode (plan/build) | Persisted as mode field in membrane state |
| Boyer decision state | Persisted as decision field |

The csos-living agent is the default. It transitions between plan and build automatically via the Boyer decision gate. No agent selection needed.
