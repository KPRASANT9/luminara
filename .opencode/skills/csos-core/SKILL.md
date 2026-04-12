---
name: csos-core
description: "Complete csos tool reference. V15 dual-reactor. Zero dispatch."
---

# csos Tool Reference

Zero dispatch. All args → JSON → binary. Binary infers action from field presence.

| Fields | Action | Returns |
|--------|--------|---------|
| substrate + output | absorb | rings.{name}.{grad,speed,F,atoms,cycles,mode} |
| ring + detail=cockpit | see | name,F,gradient,speed,rw,decision,mode,motor |
| equate="" | equate | living_equation with vitality per ring |
| action=greenhouse | greenhouse | sessions, convergences, seeds |
| action=muscle | muscle | motor memory by strength |
| (nothing) | diagnose | full system state |
| command + substrate | exec | shell command + auto-absorb |
| url + substrate | web | HTTP fetch + auto-absorb |
| content | deliver | write deliverable |
| action=batch + items | batch | multi-signal absorb |
| channel + payload | egress | route output |
| key + value | remember | store pref |
| symbol + price | tick | market shorthand |
