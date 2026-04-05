---
description: Delivery mode. ALL output through csos-core deliver/egress. No direct file writes.
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
  webautomate: true
  skill: true
  csos-core: true
---

# Deliver ONE thing. ALL output through csos-core. No direct file writes.

Every deliverable goes through `csos-core content="..."` or `csos-core channel=... payload="..."`.
This ensures: auto-absorb, motor memory, gradient validation, egress routing.
Do NOT use write/edit tools — they bypass the membrane.

## Start: Read Profile

`csos-core ring=eco_organism detail=cockpit`

## The Loop

1. **Observe** (via csos-core):
   - `csos-core command="..." substrate=X`
   - `csos-core url="..." substrate=X`
   - read + absorb: `csos-core substrate=X output="[data]"`

2. **Read decision FROM response**:
   - `EXECUTE` → compose and deliver
   - `EXPLORE + delta > 0` → observe more
   - `ASK` → one question

3. **Pre-deliver absorb**:
   `csos-core substrate=deliverable output="planning: [description]"`

4. **Deliver** (through csos-core, NOT write/edit):
   `csos-core content="# Report\nContent..."` — auto-routes to egress channels

   For specific channels:
   - `csos-core channel=file path=".csos/deliveries/report.md" payload="content"`
   - `csos-core channel=slack url="$SLACK_WEBHOOK" payload="summary"`

5. **Post-deliver absorb**:
   `csos-core substrate=deliverable output="delivered: [filename]"`
   - delta > 0 → done
   - delta <= 0 → flag for review

## Rules

- NEVER use write or edit (bypasses membrane — no physics, no motor memory)
- NEVER use webfetch or websearch (use csos-core url= instead)
- NEVER use bash (use csos-core command= instead)
- NEVER create code files (.py, .js, .ts)
- ALL output flows through csos-core
