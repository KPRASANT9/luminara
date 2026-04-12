# CSOS V13 F→0 Playbook

One binary. One tool. One membrane. Every command flows through `membrane_absorb()`.

## 0. Boot

```bash
make && ./csos --test && ./csos --seed
```

## 1. Feed Signals (any domain)

```bash
csos substrate=cpu output="usage 78% load 2.4"
csos substrate=equity_tick output="SPY 523.41 bid=523.40 ask=523.42 vol=45M"
csos substrate=candidate output="score 85 role senior_eng"
csos substrate=anything output="numbers and text"
```

Per signal: Gouterman → Causal inject → Marcus → Mitchell → Förster → Boyer → Calvin → Hierarchy → Info decay → Adversarial → Self-impact → Agent-type → Order book → Active inference.

## 2. Read Physics

```bash
csos ring=eco_organism detail=cockpit    # Quick state
csos equate                               # Full thermo
csos explain=eco_domain                   # Human-readable
csos                                      # Diagnose
```

| Field | Act when |
|---|---|
| `decision=EXECUTE` | Deliver result |
| `decision=EXPLORE, delta>0` | Keep feeding |
| `decision=EXPLORE, delta=0` | Feed probe_target |
| `decision=ASK` | Ask human ONE question |
| `probe_target=X` | X has highest uncertainty |
| `dF/dt < 0` | Learning — leave alone |
| `dF/dt > 0, 5+ cycles` | Regime changed — investigate |
| `reflexivity_ratio > 2.0` | Self-impact — reduce activity |
| `info_remaining < 0.2` | Stale — refresh substrate |

## 3. Market Operations (same tool, same agents)

```bash
csos substrate=equity_tick output="SPY 523.41 bid=523.40 ask=523.42"
csos ring=eco_organism detail=cockpit    # Regime from active_regime field
csos equate                               # F, dF/dt, edge, crowding
csos ring=eco_domain detail=full          # All atoms with edge/crowding
```

Trade decision: Boyer EXECUTE + edge>0.3 + crowding<0.5 + regime≠CRISIS + reflexivity<3.0

## 4. Risk (from physics)

| Condition | Threshold | Action |
|---|---|---|
| regime=CRISIS | -- | EXIT ALL |
| vitality<0.3 | -- | REDUCE 50% |
| dF/dt>0, 5 cycles | -- | Model wrong |
| edge<0.1 | -- | Alpha dead |
| crowding>0.8 | -- | Crowded |
| reflexivity>3.0 | -- | Self-impact |
| info_remaining<0.1 | -- | Stale |

## 5. The Loop

```
FEED → membrane_absorb() → READ photon → ACT → REPEAT
```

F decreases. Motor strengthens. Calvin discovers. Hierarchy compresses. The model approaches the thermodynamic floor.

```
F = E_q[log q(ψ) − log p(s,ψ)]
```
