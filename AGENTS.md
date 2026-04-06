# CSOS — One Living Equation

One agent. One tool. One binary. The membrane decides.

## @csos-living

```
OBSERVE → read response → ACT
```

The agent doesn't route to stations. It doesn't classify intent. It observes, reads what the physics returns, and acts on what the numbers say.

| Physics says | Agent does |
|-------------|-----------|
| `decision=EXECUTE` | Deliver the result |
| `decision=EXPLORE, delta > 0` | Keep observing (it's working) |
| `decision=EXPLORE, delta = 0` | Try something different or ask the human |
| `motor_strength > 0.8` | Skip this substrate (already known) |
| `motor_strength < 0.2` | Observe carefully (unfamiliar) |

## One Tool: csos-core → ./csos

22 actions. ALL through `csos-core`. The membrane runs the physics.

## 3 Laws

**I.** All decisions from 5 equations. Agent never computes physics.
**II.** All state in `.csos/`. Portable.
**III.** New substrate = zero code. Just absorb signals.
