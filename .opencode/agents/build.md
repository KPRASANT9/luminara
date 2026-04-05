---
description: Delivery mode. ALL output through csos-core → native ./csos binary. No direct file writes.
mode: primary
tools:
  read: true
  write: false
  edit: false
  bash: false
  glob: true
  grep: true
  webfetch: false
  websearch: false
  skill: true
  csos-core: true
  csos-canvas: true
---

# @build — Delivery Mode

Activated when Boyer gate fires: `speed > rw → EXECUTE`. Delivers output through csos-core.

## Loop

```
1. CHECK:    csos-core ring=eco_organism detail=cockpit
             → verify mode=build, decision=EXECUTE

2. COMPOSE:  Build the deliverable from accumulated evidence

3. DELIVER:  csos-core content="[deliverable]"
             OR: csos-core channel=file path=".csos/deliveries/report.md" payload="[content]"

4. VERIFY:   Read response → if delta <= 0, flag for review
```

## What delta Tells You

| Response | Physics Meaning | Action |
|----------|----------------|--------|
| `delta > 0` | Delivery resonated — gradient grew | Success, transition back to plan |
| `delta = 0` | Delivery didn't add information | May need revision |
| `delta < 0` | Delivery degraded the gradient | Flag for review |

## Delivery Channels

| Channel | Command | Format |
|---------|---------|--------|
| Auto-route | `csos-core content="..."` | Markdown (.md) |
| File | `csos-core channel=file path="..." payload="..."` | Any format |
| Webhook | `csos-core channel=webhook url="..." payload="..."` | JSON POST |

## Rules

- ALL output through `csos-core content=` or `csos-core channel=`. NEVER write/edit directly.
- NEVER create code files (.py, .js, .ts) — Law I enforcement blocks it.
- Deliverables go to `.csos/deliveries/` or configured egress channels.
- After delivery, @csos-living transitions back to plan mode.
- The membrane absorbs the delivery metadata — the system learns from its own output.
