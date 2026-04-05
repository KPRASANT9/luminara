---
name: csos-core
description: THE ONLY tool. All data in, all output out. 20 actions through unified membrane.
---

# CSOS Core — The Only Tool

ALL data flows through csos-core. No other tool writes output or fetches external data.

## Why Only csos-core?

| Other tool | Why denied | csos-core equivalent |
|-----------|-----------|---------------------|
| write/edit | Bypasses membrane — no auto-absorb, no motor memory, no gradient | `csos-core content="..."` (auto-routes through egress) |
| webfetch | Bypasses auto-absorb — physics never sees the response | `csos-core url="..."` (auto-absorbs into membrane) |
| websearch | Same bypass | `csos-core url="https://search..."` |
| bash | Bypasses law enforcement — could create code files | `csos-core command="..."` (checks forbidden patterns) |

## The 20 Actions

```bash
# INGRESS (signals enter the membrane)
csos-core command="kubectl get pods" substrate=k8s     # exec: CLI + auto-absorb
csos-core url="https://api.example.com" substrate=api  # web: fetch + auto-absorb
csos-core substrate=manual output="42.5 data here"     # absorb: bridge read/grep

# OBSERVATION (read physics state)
csos-core ring=eco_organism detail=cockpit             # see: gradient, speed, decision
csos-core ring=eco_organism detail=full                # see: + motor memory top
csos-core explain=eco_organism                         # explain: human-readable reasoning

# MEMORY (persist across sessions)
csos-core key=target_role value="Senior Android"       # remember: store human data
csos-core key=skill:k8s:check value="kubectl get pods" # remember: record a skill
csos-core key=recall                                   # recall: retrieve all stored data

# EGRESS (output exits the system)
csos-core content="# Report\nAll systems normal."      # deliver: auto-route to egress channels
csos-core channel=file path=".csos/deliveries/r.md" payload="content"  # egress: specific channel
csos-core channel=slack url="$SLACK_WEBHOOK" payload="alert"           # egress: slack

# SYSTEM
# no args = diagnose (health check)
```

## The Critical Rule

When using read/glob/grep to examine files, the results are NOT in the physics.
Always bridge them:
```
1. result = read("some/file.txt")
2. csos-core substrate=codebase output="[paste result here]"
```
This ensures: motor memory tracks the substrate, gradient grows, Boyer can decide.
