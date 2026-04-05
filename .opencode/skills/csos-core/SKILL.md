---
name: csos-core
description: THE ONLY tool. 22 actions through native ./csos binary (LLVM JIT). All physics computed in membrane_absorb().
---

# csos-core — 22 Actions

Every action routes through the native `./csos` binary. One `membrane_absorb()` call runs all 5 equations. Every response carries `decision`, `delta`, `motor_strength`.

## PHYSICS (signals enter the membrane)

```bash
# absorb: feed raw data through 3 compartments (eco_domain → cockpit → organism)
csos-core substrate=databricks output="revenue 42M users 1.2M ARR 180M"

# exec: run CLI command, auto-absorb stdout (Law I: blocks .py/.js/.ts creation)
csos-core command="kubectl get pods -n prod" substrate=k8s

# web: fetch URL, auto-absorb response (auth-aware, cookie persistence)
csos-core url="https://api.example.com/health" substrate=infra

# fly: inject signals directly into a ring
csos-core ring=eco_domain signals="42.5,100,3.14"

# batch: multiple absorbs in one call
csos-core items='[{"substrate":"a","output":"1"},{"substrate":"b","output":"2"}]'
```

## OBSERVATION (read physics state)

```bash
# see: read ring state (minimal | standard | cockpit | full)
csos-core ring=eco_organism detail=cockpit
# Returns: mode, speed, rw, action_ratio, motor_entries, gradient

# explain: human-readable reasoning
csos-core explain=eco_organism
# Returns: natural language explanation of current state

# diagnose: full system health check (no args)
csos-core
# Returns: status (healthy|degraded), issues, ring health

# ping: liveness check
# (via JSON pipe: {"action":"ping"})
```

## MEMORY (persist across sessions)

```bash
# remember: store human data
csos-core key=target_role value="Senior Android developer, 180-220k"
csos-core key="skill:k8s:check" value="kubectl get pods -n prod"

# recall: retrieve all stored data
csos-core key=recall

# profile: human profile summary
# (via JSON pipe: {"action":"profile"})
```

## DELIVERY (output exits the system)

```bash
# deliver: auto-route to configured channels
csos-core content="# Cost Analysis\n\nTotal: $42,000/month..."

# egress: specific channel
csos-core channel=file path=".csos/deliveries/report.md" payload="# Report content"
csos-core channel=webhook url="$SLACK_WEBHOOK" payload="Alert: CPU spike"
```

## TOOLS (evolve the system itself)

```bash
# tool: write to sanctioned paths (.opencode/tools/, specs/, .csos/deliveries/)
csos-core toolpath="specs/new_substrate.csos" body="atom new { ... }"

# toolread: read from sanctioned path
csos-core toolread=".opencode/agents/csos-living.md"

# toollist: list sanctioned directory
csos-core toollist="specs/"
```

## ADVANCED (via JSON pipe)

These 8 actions are available through direct JSON to the binary:

```bash
# grow: create a new ring
echo '{"action":"grow","ring":"my_ring"}' | ./csos

# diffuse: Forster coupling between rings
echo '{"action":"diffuse","source":"eco_domain","target":"eco_organism"}' | ./csos

# lint: health check on specific ring
echo '{"action":"lint","ring":"eco_domain"}' | ./csos

# muscle: motor memory priorities (spaced repetition)
echo '{"action":"muscle","ring":"eco_organism"}' | ./csos

# hash: deterministic substrate hash (1000-9999)
echo '{"action":"hash","substrate":"databricks"}' | ./csos

# save: persist all ring state to disk
echo '{"action":"save"}' | ./csos

# ping: alive check
echo '{"action":"ping"}' | ./csos

# profile: human data summary
echo '{"action":"profile"}' | ./csos
```

## Response Format

Every absorb/exec/web response:

```json
{
  "substrate": "databricks",
  "signals": 15,
  "physics": {
    "decision": "EXECUTE",
    "delta": 15,
    "motor_strength": 0.82,
    "interval": 3,
    "resonated": true,
    "mode": "build",
    "domain": {"grad": 4526, "speed": 161.6, "F": 29.2},
    "cockpit": {"grad": 11297, "speed": 209.2},
    "organism": {"grad": 14434, "speed": 801.9, "rw": 0.833}
  }
}
```

## Physics Constants (from membrane.h)

All derived from the 5 equations. Zero magic numbers.

| Constant | Value | Equation | Controls |
|----------|-------|----------|----------|
| `CSOS_BOYER_THRESHOLD` | 0.3 | Boyer | Decision gate: action_ratio > 0.3 → EXPLORE |
| `CSOS_DEFAULT_RW` | 0.833 | Gouterman | Resonance width for empty rings |
| `CSOS_MOTOR_GROWTH` | 0.1 | Forster | Strength gain on spaced encounter |
| `CSOS_MOTOR_DECAY` | 0.99 | Forster | Per-cycle strength decay |
| `CSOS_CALVIN_FREQUENCY` | 5 | Calvin | Synthesis attempt every 5 cycles |
| `CSOS_STUCK_CYCLES` | 2 | Boyer | Consecutive zero-delta → mode switch |
