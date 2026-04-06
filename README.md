# CSOS

Single native binary. Five photosynthetic equations. Zero Python.

A 95KB C binary that turns any LLM into a physics-grounded autonomous system. Equations loaded from spec files at runtime. LLVM JIT compiles them to native SIMD. 7,231 absorb operations/second. 27 stress tests. One process serves physics, HTTP, SSE, and the workflow canvas.

```
./csos --http 4200
```

Open `http://localhost:4200` in a browser.

---

## Table of Contents

- [Quick Start](#quick-start)
- [Architecture](#architecture)
- [The 5 Equations](#the-5-equations)
- [The 7 Organelles](#the-7-organelles)
- [How It Works](#how-it-works)
- [Build System](#build-system)
- [Git Workflow](#git-workflow)
- [HTTP API](#http-api)
- [22 Actions](#22-actions)
- [Physics Constants](#physics-constants)
- [Canvas](#canvas)
- [Agents](#agents)
- [HOWTO Playbooks](#howto-playbooks)
- [Project Structure](#project-structure)
- [The 3 Laws](#the-3-laws)

---

## Quick Start

```bash
# Build
make

# Plant the seed (one command — everything grows from here)
./csos --seed

# Or with a profile:
./csos --seed ops          # 24/7 infrastructure monitoring
./csos --seed devops       # Codebase intelligence
./csos --seed pipeline     # Data pipeline orchestration

# Open http://localhost:4200 in browser
```

The seed creates `.csos/` state, grows 3 ecosystem rings, plants initial substrates, starts the HTTP daemon with canvas. Feed it signals and it grows — motor memory strengthens, Calvin discovers patterns, Boyer makes decisions.

### Other ways to run

```bash
./csos --http 4200         # HTTP daemon (no seed, uses existing state)
echo '{"action":"ping"}' | ./csos   # CLI pipe mode
make test                  # 27 stress tests
make hooks                 # Install git pre-commit hook
```

### Prerequisites

| Requirement | Purpose | Required? |
|-------------|---------|-----------|
| C compiler (cc/clang) | Build the binary | Yes |
| LLVM 18+ (`brew install llvm`) | JIT compilation of equations to SIMD | No (falls back to C interpreter) |
| Browser | Workflow canvas at `http://localhost:4200` | No (CLI works without) |
| OpenCode (`npm i -g opencode-ai`) | Agent orchestration | No (binary works standalone) |

---

## The Plant Lifecycle

CSOS is modeled on photosynthesis. The lifecycle maps directly:

```
SEED          ./csos --seed ops                 One command. Plant it.
              Creates .csos/, grows 3 rings,
              plants initial substrates.
                    │
SOIL          .csos/ directory                  State accumulates here.
              Volume-mount it. Transplant it.
              cp -r .csos/ /new/host/ → done.
                    │
WATER         Feed signals continuously.        The system absorbs.
              Metrics, logs, events, data.
              Cron, webhook, agent, manual.
                    │
SUNLIGHT      5 equations run automatically.    Physics drives growth.
              Gouterman → Marcus → Mitchell
              → Forster → Boyer → Calvin.
                    │
ROOTS         Motor memory strengthens.         It learns what matters.
              Services checked often → strong.
              Noise → decays (0.99/cycle).
                    │
LEAVES        Atoms absorb signal types.        Spectral routing.
              cpu_resonance catches CPU.
              latency_gate catches latency.
                    │
FLOWERS       Calvin atoms emerge.              Patterns discovered.
              "CPU 78% + no deploy" →
              new atom, recognized next time.
                    │
FRUIT         Boyer fires: EXECUTE.             Harvest the decision.
              Alert, report, action, fix.
              speed > rw = sufficient evidence.
                    │
SEEDS         .csos/ + specs/                   Transplant anywhere.
              Copy state to new host.
              Same gradient, same memory,
              same learned patterns. No cold start.
```

### Transplanting (Moving the System)

```bash
# On old host:
tar czf csos-state.tar.gz .csos/ specs/

# On new host:
tar xzf csos-state.tar.gz
./csos --http 4200
# Gradient, motor memory, Calvin atoms — all restored. Zero cold start.
```

---

## Architecture

```
specs/eco.csos          5 equations defined as data (the source of truth)
       |
   spec_parse.c         Parsed at runtime (no hardcoded arrays)
       |
   formula_jit.c        Compiled to LLVM IR → native SIMD (optional)
       |
   membrane.c           ONE function: membrane_absorb()
       |                Gouterman → Marcus → Mitchell → Forster → Boyer → Calvin
       |                All 7 organelles in one pass. No layers.
       |
   protocol.c           22 JSON actions + HTTP server + SSE
       |
   ./csos (95KB)        Single binary. Single process.
       |
       +--  GET /           Canvas HTML (workflow builder)
       +--  GET /events     SSE stream (Server-Sent Events)
       +--  GET /api/state  Full organism state as JSON
       +--  POST /api/command   JSON action → physics → response
```

### What Runs Where

| Component | Process | Communication |
|-----------|---------|---------------|
| Physics engine | `./csos` | In-process (membrane_absorb()) |
| HTTP server | `./csos --http` | TCP socket, port 4200 |
| SSE stream | `./csos --http` | Persistent HTTP connection |
| Canvas | Browser | EventSource → daemon SSE |
| Agent (OpenCode) | Separate process | stdin/stdout JSON pipe to `./csos` |
| State | `.csos/rings/*.json` | Filesystem (volume-mountable) |

### Data Flow

```
Signal enters (any protocol)
    |
    v
membrane_absorb(value, substrate_hash, protocol)
    |
    +-- Motor trace: spaced repetition strength update
    +-- Gouterman: spectral routing to matching atoms
    +-- PSI/PSII: each atom predicts via its formula, observes actual
    +-- Marcus: error = |predicted - actual| / max(|actual|, guard)
    +-- Mitchell: gradient += resonated count
    +-- Forster: continuous coupling between rings
    +-- Boyer: speed > rw → EXECUTE, else EXPLORE
    +-- Calvin: every 5 cycles, synthesize new atoms from non-resonated patterns
    |
    v
csos_photon_t returned (carries ALL context in one struct)
    decision, delta, motor_strength, interval, resonated, mode
```

---

## The 5 Equations

Defined in `specs/eco.csos`. Loaded at runtime by `spec_parse.c`. JIT-compiled to native code by `formula_jit.c`. Adding a 6th equation requires zero code changes — only a new `atom` block in the spec.

| # | Equation | Formula | Compute Expression | Role |
|---|----------|---------|-------------------|------|
| 1 | **Gouterman 1961** | dE = hc/lambda | `h * c / l` | Does this signal match known patterns? |
| 2 | **Forster 1948** | k_ET = (1/tau)(R0/r)^6 | `(1/t) * (R0/r) ** min(R0/r, 6)` | Can knowledge transfer between domains? |
| 3 | **Marcus 1956** | k = exp(-(dG+lambda)^2/4lambda*kT) | `exp(-(dG+l)**2/(4*l*V)) * input` | How far off is prediction from reality? |
| 4 | **Mitchell 1961** | dG = -nF*dpsi + 2.3RT*dpH | `n*F*abs(dy) + signal*abs(dy)/(1+n)` | How much evidence accumulated? |
| 5 | **Boyer 1997** | ATP = flux*n/3 | `flux * n / 3` | Enough evidence to decide? |

---

## The 7 Organelles

| # | Organelle | What It Does | Implementation |
|---|-----------|-------------|----------------|
| 1 | Antenna Complex (LHC) | Routes signals to matching atoms by spectral range | `atom.spectral[2]` + `broadband` flag |
| 2 | NPQ / Xanthophyll | Clamps parameters to prevent drift | `CSOS_TUNE_THRESHOLD` (10% of rw) |
| 3 | Photosystem I & II | Each atom predicts via its formula, then observes actual | `formula_jit.c` or `formula_eval.c` |
| 4 | C4 Pre-Concentration | Validates Calvin patterns: min 5 signals, low variance, no overlap | `membrane_calvin()` in membrane.c |
| 5 | Thylakoid Compartments | Per-substrate gradient tracking | Hash-based routing, motor trace |
| 6 | Continuous FRET | Energy transfer between rings every cycle | Forster efficiency with `CSOS_FORSTER_EXPONENT` |
| 7 | D1 Protein Repair | Prune old photons, reset drifted parameters | Photon ring buffer (8192 entries) |

---

## How It Works

### The Decision Loop

```
SIGNAL IN                                    DECISION OUT
    |                                            ^
    v                                            |
 Absorb signal                              EXECUTE → deliver result
    |                                       EXPLORE → observe more
    v                                       ASK     → ask human
 Physics runs                               STORE   → save for later
 (5 equations,                                  ^
  7 organelles,                                 |
  one function)                             Boyer gate:
    |                                       speed > rw?
    v
 Photon returned
 {decision, delta, motor_strength}
```

### The 3 Rings

| Ring | Layer | Purpose |
|------|-------|---------|
| `eco_domain` | Substrate signals | Absorbs all external signals. Calvin atoms grow per substrate. |
| `eco_cockpit` | Agent wisdom | Tracks specificity_delta, action_ratio, calvin_rate. |
| `eco_organism` | Integration | Aggregates domain + cockpit. Boyer decision gate lives here. |

### Motor Memory (Spaced Repetition)

Every substrate builds muscle memory through the spaced repetition motor:

| Encounter Pattern | Strength Growth | Priority |
|-------------------|----------------|----------|
| Increasing intervals (1, 3, 7, 15...) | `CSOS_MOTOR_GROWTH * spacing_factor` (fast) | High — reliably important |
| Fixed intervals (crammed) | `CSOS_MOTOR_BACKOFF` (slow) | Medium — short-term urgency |
| Single encounter | Initial only | Low — likely noise |

Strength decays by `CSOS_MOTOR_DECAY` (0.99) each cycle. Capped at 1.0.

---

## Build System

### Makefile Targets

| Command | What It Does |
|---------|-------------|
| `make` | Build with LLVM JIT (auto-detected). Falls back to C-only. |
| `make nojit` | Force C-only build (no LLVM dependency) |
| `make test` | Run 27 native stress tests |
| `make bench` | Benchmark membrane_absorb() throughput |
| `make validate` | Verify spec consistency + Law I + zero magic numbers + tests |
| `make hooks` | Install git pre-commit hook |
| `make http` | Build + start HTTP daemon on port 4200 |
| `make clean` | Remove build artifacts |

### Dependency Chain

```
specs/eco.csos   ---+
lib/membrane.h   ---+--->  make  --->  ./csos (95KB)
src/native/*.c   ---+
```

Changing any spec, header, or C source triggers a rebuild. The spec file is a first-class Make dependency.

### With vs Without LLVM

| Feature | With LLVM | Without LLVM |
|---------|-----------|--------------|
| Equation execution | JIT-compiled to native SIMD | C interpreter (`formula_eval.c`) |
| Absorb loop | Vectorized via LLVM ORC JIT | Standard C loop |
| Calvin recompilation | Hot-reload: new atoms JIT'd immediately | Interpreter handles new atoms |
| Binary size | ~95KB | ~60KB |
| Performance | ~7,200 ops/sec | ~2,000 ops/sec |

---

## Git Workflow

### Pre-Commit Hook

Install once with `make hooks`. Every `git commit` runs 6 automated checks:

```
1. specs/eco.csos exists
2. Every atom has a compute expression (Law I)
3. No hardcoded EQUATIONS[] arrays in membrane.c (Law I)
4. Zero bare magic numbers in membrane.c (all from membrane.h constants)
5. Auto-rebuild binary if spec or source changed (+ git add)
6. 27/27 native tests pass
```

All checks are pure shell — zero Python dependency.

### What Gets Blocked

| Violation | Error |
|-----------|-------|
| Missing compute expression | `FAIL: 5 atoms but 4 compute expressions` |
| Hardcoded equation array | `FAIL: Hardcoded EQUATIONS[] in membrane.c` |
| Bare magic number (e.g. `0.833`) | `FAIL: 1 bare magic numbers` + shows the line |
| Test regression | `FAIL: 25/27 TESTS PASSED` |
| Build failure | `FAIL: Build failed` |

### What Happens Automatically

- Changed `.c`, `.h`, or `.csos` → binary rebuilt and staged (`git add csos`)
- Build fails → commit blocked
- Tests fail → commit blocked
- All pass → commit proceeds

---

## HTTP API

Start with `./csos --http 4200`. All routes support CORS.

### GET Routes

| Route | Response | Purpose |
|-------|----------|---------|
| `GET /` | HTML | Canvas UI (`.canvas-tui/index.html`) |
| `GET /events` | SSE stream | Server-Sent Events. Initial `state` event, then `response` on every command. Keepalive every 5s. |
| `GET /api/state` | JSON | Full organism: rings, gradients, speeds, decisions, client count |
| `GET /api/templates` | JSON | 6 living workflow templates |

### POST Routes

| Route | Body | Response | Purpose |
|-------|------|----------|---------|
| `POST /api/command` | JSON action | JSON result | Execute any of 22 actions. Response also broadcast via SSE. |

### SSE Events

Connect via `new EventSource('/events')` from browser.

| Event | When | Data |
|-------|------|------|
| `state` | On connect | Full organism state (rings, gradients, decisions) |
| `response` | Every POST /api/command | Command result (decision, delta, motor_strength) |
| `: keepalive` | Every 5 seconds | Empty comment (prevents timeout) |

---

## 22 Actions

Send as JSON to `POST /api/command` or pipe to stdin.

### Physics

| Action | Input | Output | Physics |
|--------|-------|--------|---------|
| `absorb` | `substrate`, `output` | decision, delta, motor_strength | Full 3-ring cascade |
| `fly` | `ring`, `signals` | gradient, speed, F | Direct ring stimulation |
| `grow` | `ring` | atom count | Create new ring |
| `diffuse` | `source`, `target` | atoms transferred | Forster coupling between rings |
| `see` | `ring`, `detail` | ring state | Read (minimal/standard/cockpit/full) |
| `lint` | `ring` | pass/fail + issues | Health check |
| `diagnose` | — | status, issues | Full system diagnostic |
| `ping` | — | alive, rings, speed | Liveness check |

### External I/O

| Action | Input | Output | Notes |
|--------|-------|--------|-------|
| `exec` | `command`, `substrate` | stdout + physics | Law I enforced (blocks .py/.js creation) |
| `web` | `url`, `substrate`, `steps` | response + physics | Auth-aware, cookie persistence |
| `hash` | `substrate` | hash (1000-9999) | Deterministic substrate hash |
| `muscle` | `ring` | top substrates by strength | Motor memory priorities |

### Human Data

| Action | Input | Output |
|--------|-------|--------|
| `remember` | `key`, `value` | stored in `.csos/sessions/human.json` |
| `recall` | — | all stored key-value pairs |
| `profile` | — | human profile summary |

### Delivery

| Action | Input | Output |
|--------|-------|--------|
| `deliver` | `content` | auto-routed to appropriate channel |
| `egress` | `channel`, `payload`, `path`/`url` | file write or webhook POST |
| `explain` | `ring` | human-readable reasoning |

### Tool Management

| Action | Input | Output |
|--------|-------|--------|
| `tool` | `path`, `body` | write to sanctioned path (.opencode/, specs/, .csos/deliveries/) |
| `toolread` | `path` | read from sanctioned path |
| `toollist` | `dir` | list sanctioned directory |
| `save` | — | persist all ring state to disk |

---

## Physics Constants

All defined in `lib/membrane.h`. Every constant traces to one of the 5 equations with documented derivation. Zero magic numbers in `membrane.c`.

### Equation-Derived Constants

| Constant | Value | Equation | Derivation |
|----------|-------|----------|------------|
| `CSOS_DEFAULT_RW` | 0.8333 | Gouterman | 5/6 dof for standard 5-param atom |
| `CSOS_FORMULA_DOF_DIVISOR` | 10 | Gouterman | ~10 chars per independent formula term |
| `CSOS_ERROR_DENOM_GUARD` | 0.01 | Marcus | 1% of signal = noise floor |
| `CSOS_EXP_CLAMP` | 20.0 | Marcus | exp(20) ~ 5e8, beyond is unphysical |
| `CSOS_FORSTER_EXPONENT` | 2 | Forster | r^-6 molecular → r^-2 membrane-scale |
| `CSOS_MOTOR_GROWTH` | 0.1 | Forster | LHC-II per-hop efficiency 0.95 → gain = 0.05, doubled |
| `CSOS_MOTOR_BACKOFF` | 0.02 | Forster | 1/5 of growth rate (cramming penalty) |
| `CSOS_MOTOR_DECAY` | 0.99 | Forster | D1 protein half-life: 1% loss per cycle |
| `CSOS_MOTOR_MAX_SF` | 3.0 | Forster | R0/r at r = R0/3, clamped for macro-scale |
| `CSOS_TUNE_THRESHOLD` | 0.1 | Mitchell | 10% proton leak rate |
| `CSOS_CALVIN_GRAD_FRAC` | 0.05 | Mitchell | 5% of signal mean = minimum detectable gradient |
| `CSOS_CALVIN_GRAD_FLOOR` | 0.1 | Mitchell | Absolute minimum observable proton-motive force |
| `CSOS_BOYER_THRESHOLD` | 0.3 | Boyer | 1/3 catalytic sites (3 protons per ATP) |
| `CSOS_STUCK_CYCLES` | 2 | Boyer | 3 catalytic sites - 1 = stall detection |
| `CSOS_CALVIN_FREQUENCY` | 5 | Calvin | Rubisco rate ~3/sec x 2s observation window |
| `CSOS_CALVIN_SAMPLE_SIZE` | 50 | Calvin | PEP carboxylase concentration ratio |
| `CSOS_CALVIN_MATCH_DEPTH` | 10 | Calvin | Rubisco discrimination: check last 10 resonated |
| `CSOS_CALVIN_VAR_MULT` | 0.1 | Calvin | Marcus confidence: 10% stdev = overlap threshold |

### System Limits

| Constant | Value | Purpose |
|----------|-------|---------|
| `CSOS_MAX_ATOMS` | 32 | Max atoms per membrane |
| `CSOS_MAX_PARAMS` | 16 | Max parameters per atom |
| `CSOS_MAX_RINGS` | 16 | Max rings per organism |
| `CSOS_MAX_MOTOR` | 256 | Max motor memory entries |
| `CSOS_PHOTON_RING` | 8192 | Photon history ring buffer |
| `CSOS_CO2_POOL_SIZE` | 256 | Calvin CO2 pool (non-resonated signals) |

---

## Canvas

Single HTML file at `.canvas-tui/index.html`, served by the native binary at `GET /`.

### Layout

| Panel | Content |
|-------|---------|
| **Left** | Templates (6 patterns), Datasources (auth config), Volumes (autoloaded artifacts), Ring state |
| **Center** | Mermaid workflow editor, rendered diagram, runtime output with per-node metrics |
| **Right** | Collaborators, Scan (see/lint/diagnose), Timeline, Command input |

### Templates

Click a template to populate the mermaid editor with a pre-configured workflow:

| Template | Connectors | Pattern |
|----------|-----------|---------|
| Data Pipeline | http → webhook | Ingest → Process → Decide → Deliver |
| Database ETL | database → database → webhook | Extract → Transform → Load → Validate |
| Model Inference | model → http | Prepare → Infer → Parse → Evaluate |
| File Processing | file → file | Read → Parse → Analyze → Output |
| RDMA HPC | rdma → rdma | Register → Compute → Transfer → Verify |
| Multi-Source | http + database + file → webhook | Multiple sources → Merge → Analyze → Deliver |

### Workflow Execution

1. Write or select mermaid diagram in the editor
2. Click **Run** — daemon parses mermaid, executes nodes in topological order
3. Each node runs through `membrane_absorb()` — physics metrics streamed via SSE
4. Completed nodes highlight in the diagram (green = success, red = fail)
5. Download results as JSON, CSV, or TXT

---

## Agents

Three agent definitions in `.opencode/agents/`. One tool (`csos-core.ts`) spawns the native binary.

| Agent | Mode | Purpose |
|-------|------|---------|
| `@csos-living` | Primary | Full entropy loop. Transitions plan ↔ build via Boyer gate. |
| `@plan` | Observation | Read-only. Investigate, analyze, research. |
| `@build` | Delivery | Write deliverables (.md, .csv, .docx) via csos-core. |

### How Agents Read Physics

Every `csos-core` response includes the decision. The agent reads 3 numbers and knows what to do:

```json
{"decision": "EXECUTE", "delta": 5, "motor_strength": 0.82, "mode": "build"}
```

| Reading | Agent Action |
|---------|-------------|
| `decision = EXECUTE` | Deliver result via `csos-core content="..."` |
| `delta > 0` | Tools are yielding — observe more |
| `delta = 0, stalled` | Ask human ONE question |
| `motor_strength > 0.8` | Substrate well-understood — skip re-reading |

---

## HOWTO Playbooks

### HOWTO: Add a New Equation

Edit `specs/eco.csos`:

```
atom planck {
    formula:   E = h * f;
    compute:   h * f;
    source:    "Planck 1900";
    params:    { h: 1.0, f: 1.0 };
    spectral:  [100, 5000];
    broadband: false;
}
```

Then rebuild:

```bash
make        # spec_parse.c loads it at runtime
make test   # verify 27/27 still pass
```

Zero code changes required. The spec IS the code.

### HOWTO: Add a New Substrate

```bash
echo '{"action":"absorb","substrate":"my_service","output":"cpu 45.2 memory 72.1"}' | ./csos
```

The system auto-creates a motor memory entry, routes through Gouterman spectral filter, and starts building gradient. Calvin synthesis may create a new atom if the pattern is novel.

### HOWTO: Monitor Ring Health

```bash
# Quick check
echo '{"action":"ping"}' | ./csos

# Detailed cockpit view
echo '{"action":"see","ring":"eco_organism","detail":"cockpit"}' | ./csos

# Full diagnostic
echo '{"action":"diagnose"}' | ./csos

# Lint specific ring
echo '{"action":"lint","ring":"eco_domain"}' | ./csos
```

### HOWTO: Run as HTTP Daemon

```bash
./csos --http 4200
# Canvas:  http://localhost:4200
# SSE:     http://localhost:4200/events
# API:     http://localhost:4200/api/state
# Command: curl -X POST http://localhost:4200/api/command \
#            -H 'Content-Type: application/json' \
#            -d '{"action":"absorb","substrate":"test","output":"hello 42"}'
```

### HOWTO: Run via Docker

```bash
docker compose up -d
# Canvas at http://localhost:4200
# State persisted to ./csos-data/
```

### HOWTO: Use With OpenCode Agent

```bash
npm i -g opencode-ai
opencode
> What is our infrastructure health?     # @csos-living auto-observes
> Write a cost analysis report           # transitions to build mode via Boyer
```

### HOWTO: Run Headless (Cron/Server)

```bash
# Watchdog (health check + auto-heal)
bash scripts/watchdog.sh

# Crontab
*/15 * * * * /path/to/scripts/watchdog.sh
```

### HOWTO: Add a Physics Constant

1. Add `#define` to `lib/membrane.h` with equation derivation comment
2. Use the constant in `membrane.c` (never bare numbers)
3. `make validate` checks for bare magic numbers
4. Pre-commit hook blocks commits with undocumented constants

### HOWTO: Set Up Git Hooks

```bash
make hooks
# Installs .git/hooks/pre-commit
# Every commit now validates: spec, Law I, magic numbers, build, 27 tests
```

### HOWTO: Benchmark Performance

```bash
make bench
# membrane_absorb(): 10000 ops in 1.4s = 138us/op (7231 ops/sec)
# Each call: motor trace + 5 atom resonance + gradient + Boyer + Calvin
```

### HOWTO: Debug a Decision

```bash
# See what the organism decides and why
echo '{"action":"see","ring":"eco_organism","detail":"cockpit"}' | ./csos
# Returns: mode, speed, rw, action_ratio, motor_entries, gradient

# Get human-readable explanation
echo '{"action":"explain","ring":"eco_organism"}' | ./csos

# Check motor memory priorities
echo '{"action":"muscle","ring":"eco_organism"}' | ./csos
# Returns: top substrates ranked by spaced-repetition strength
```

---

## Project Structure

```
V12/
|-- csos                         95KB native binary (THE system)
|-- Makefile                     Build system (LLVM auto-detected)
|-- Dockerfile                   Single-stage native build
|-- docker-compose.yml           5-line production config
|
|-- lib/                         Headers (664 lines)
|   |-- membrane.h               THE data structure + 18 physics constants
|   |-- page.h, record.h,        Foundation types
|   |-- ring.h, index.h
|
|-- src/native/                  C source (4,100 lines)
|   |-- csos.c                   Entry point + 9 stress tests + 5 modes
|   |-- membrane.c               Physics engine (5 equations, 7 organelles)
|   |-- protocol.c               22 JSON actions + HTTP + SSE
|   |-- spec_parse.c             Runtime .csos loader
|   |-- formula_jit.c            LLVM JIT (equation → native SIMD)
|   |-- jit.c                    LLVM absorb loop vectorization
|   |-- formula_eval.c           C interpreter (fallback without LLVM)
|   |-- store.c                  JSON serialization of ring state
|   |-- page.c, record.c, ring.c Foundation implementations
|
|-- specs/                       Spec files (Law I: spec IS code)
|   |-- eco.csos                 5 equations + 3 rings + 3 agents + 3 laws
|   |-- *.csos                   Additional specs (auto-detected, mermaid, etc.)
|
|-- .canvas-tui/
|   |-- index.html               Workflow canvas (663 lines)
|
|-- .opencode/                   Agent configuration
|   |-- opencode.json            Agent wiring + permissions
|   |-- tools/csos-core.ts       THE tool (spawns ./csos binary)
|   |-- tools/csos-canvas.ts     Canvas opener
|   |-- agents/*.md              Agent definitions (living, plan, build)
|   |-- skills/                  Skill documentation
|
|-- .csos/                       Persistent state (volume-mountable)
|   |-- rings/*.json             3 ecosystem ring states
|   |-- sessions/human.json      Human data + auth config
|
|-- scripts/
|   |-- csos-startup.sh          Start daemon
|   |-- pre-commit-csos.sh       Git hook (6 checks, pure shell)
|   |-- watchdog.sh              Health monitor (native binary only)
|
|-- README.md                    This file
|-- AGENTS.md                    Agent guidance loop + Laws
```

**4,764 lines of C. 95KB binary. Zero Python.**

---

## The 3 Laws

Enforced structurally by the build system and git hooks — not by convention.

**I. No Hardcoded Logic.** All decisions flow from the 5 equations defined in `specs/eco.csos`. All constants in `membrane.h` trace to an equation. Pre-commit hook blocks bare magic numbers. `make validate` checks for hardcoded equation arrays.

**II. All State from .csos/.** Ring state, sessions, deliveries. Volume mount `.csos/` and the entire system state travels with it. `./csos --http` starts from persisted state.

**III. New Substrates = Zero Code.** Hash-based routing + Calvin synthesis handle any substrate. New equation = new atom block in the spec file. `make` rebuilds. No code changes.

---

## Measured Performance

| Metric | Value | How to reproduce |
|--------|-------|-----------------|
| Absorb throughput | **7,231 ops/sec** (138 us/op) | `make bench` |
| LLVM JIT compilation | 5 equations → native in ~10ms | Printed at startup |
| Stress tests | **27/27 pass** | `make test` |
| Binary size | **95KB** | `ls -lh csos` |
| Source code | **4,100 lines C** + 664 lines headers | `wc -l src/native/*.c lib/*.h` |
| Bare magic numbers | **0** in membrane.c | `make validate` |
| Python files | **0** | `find . -name "*.py"` |

---

## Integration & Deployment

### Deployment Topology

```
┌──────────────────────────────────────────────────────┐
│  Production Host / Container / VM                    │
│                                                      │
│  ./csos --http 4200                                  │
│  ├── Physics engine (membrane.c)                     │
│  ├── HTTP server + SSE (protocol.c)                  │
│  ├── 22 JSON actions                                 │
│  ├── LLVM JIT (formula_jit.c, optional)              │
│  └── Canvas served at GET /                          │
│                                                      │
│  Persistent state: .csos/  (volume-mountable)        │
│  Specs loaded at startup: specs/eco.csos             │
│  Canvas: .canvas-tui/index.html                      │
│                                                      │
│  Optional:                                           │
│  ├── OpenCode agent (separate process, pipes JSON)   │
│  ├── Watchdog cron (scripts/watchdog.sh)             │
│  └── External integrations (via exec/web actions)    │
└──────────────────────────────────────────────────────┘
```

### Deployment Options

| Method | Command | State Persistence | Scaling |
|--------|---------|-------------------|---------|
| **Bare metal** | `./csos --http 4200` | `.csos/` directory | Single process |
| **Docker** | `docker compose up -d` | `./csos-data` volume | Container restart |
| **Systemd** | See service file below | `/var/lib/csos/` | Process supervision |
| **Kubernetes** | Pod + PVC for `.csos/` | PersistentVolumeClaim | StatefulSet (single replica) |

### CI/CD Pipeline

```
git push
    │
    ├── Pre-commit hook (local)
    │   ├── Spec validation (atom count = compute count)
    │   ├── Law I check (no hardcoded arrays)
    │   ├── Magic number check (zero bare constants)
    │   ├── Rebuild binary if source changed
    │   └── 27/27 native tests
    │
    ├── CI (GitHub Actions / GitLab CI)
    │   ├── make clean && make           # Build with LLVM JIT
    │   ├── ./csos --test                # 27 stress tests
    │   ├── make validate                # Spec + Law I + magic numbers
    │   ├── docker build -t csos .       # Container build (includes tests)
    │   └── docker push                  # Registry
    │
    └── Deploy
        ├── docker compose pull && docker compose up -d
        └── State preserved via .csos/ volume
```

### HOWTO: GitHub Actions CI

```yaml
# .github/workflows/ci.yml
name: CSOS CI
on: [push, pull_request]
jobs:
  build-and-test:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v4
      - name: Install LLVM
        run: sudo apt-get install -y llvm-18 llvm-18-dev
      - name: Build
        run: make LLVM_PREFIX=/usr/lib/llvm-18
      - name: Test
        run: ./csos --test 2>&1 | tail -1 | grep "27/27"
      - name: Validate
        run: make validate LLVM_PREFIX=/usr/lib/llvm-18
      - name: Benchmark
        run: ./csos --bench
```

### HOWTO: Systemd Service

```ini
# /etc/systemd/system/csos.service
[Unit]
Description=CSOS Native Membrane Daemon
After=network.target

[Service]
Type=simple
ExecStart=/usr/local/bin/csos --http 4200
WorkingDirectory=/opt/csos
Restart=always
RestartSec=5

[Install]
WantedBy=multi-user.target
```

```bash
sudo cp csos /usr/local/bin/
sudo mkdir -p /opt/csos/.csos /opt/csos/specs /opt/csos/.canvas-tui
sudo cp specs/eco.csos /opt/csos/specs/
sudo cp .canvas-tui/index.html /opt/csos/.canvas-tui/
sudo systemctl enable csos && sudo systemctl start csos
```

### HOWTO: Kubernetes Deployment

```yaml
apiVersion: apps/v1
kind: StatefulSet
metadata:
  name: csos
spec:
  replicas: 1
  template:
    spec:
      containers:
      - name: csos
        image: csos:latest
        command: ["/usr/local/bin/csos", "--http", "4200"]
        ports:
        - containerPort: 4200
        volumeMounts:
        - name: csos-state
          mountPath: /app/.csos
        livenessProbe:
          httpGet: { path: /api/state, port: 4200 }
          periodSeconds: 30
  volumeClaimTemplates:
  - metadata: { name: csos-state }
    spec:
      accessModes: ["ReadWriteOnce"]
      resources: { requests: { storage: 1Gi } }
```

### DevOps Requirements

| Requirement | Why | Minimum |
|-------------|-----|---------|
| **C compiler** | Build the binary | `cc` (any), `clang` preferred |
| **LLVM 18+** | JIT compilation (optional) | `brew install llvm` or `apt install llvm-18` |
| **95KB disk** | Binary size | Trivial |
| **~10MB RAM** | 3 ecosystem rings + motor memory | Baseline |
| **1 TCP port** | HTTP/SSE daemon | 4200 default |
| **Filesystem** | `.csos/` state directory | Volume-mountable |
| **curl** | Health checks, watchdog | Standard |

**Not required:** Python, Node.js, Docker, databases, message queues, cloud services. The binary is self-contained.

---

## Autonomous Enterprise Operations

CSOS is a physics engine for operational decisions. Three concrete use cases where the 5 equations provide measured value:

### Use Case 1: 24/7 Infrastructure Monitoring

**Problem:** Alert fatigue. Thousands of metrics, most noise. Engineers trained to ignore alerts.

**How CSOS solves it:**

```bash
# Feed metrics every 30 seconds (cron or agent loop)
./csos --http 4200 &

# Absorb CPU metric
curl -X POST http://localhost:4200/api/command \
  -d '{"action":"absorb","substrate":"prod_cpu","output":"78.2 usage no_deploy"}'

# Absorb memory metric
curl -X POST http://localhost:4200/api/command \
  -d '{"action":"absorb","substrate":"prod_memory","output":"4.2GB of 8GB 52%"}'

# Check what the physics decided
curl -X POST http://localhost:4200/api/command \
  -d '{"action":"muscle","ring":"eco_organism"}'
# Returns: top substrates by motor_strength (what actually matters)
```

**Physics at work:**

| Equation | What It Does in This Context |
|----------|------------------------------|
| **Gouterman** | Routes CPU signals to cpu_resonance atom, memory to memory_pressure atom. No cross-contamination. |
| **Marcus** | Measures prediction error. "CPU was 45% yesterday, 78% today" → high error → non-resonated. |
| **Mitchell** | Accumulates gradient. 3 consecutive high-CPU readings → gradient grows → evidence builds. |
| **Calvin** | "CPU 78% + no_deploy" pattern becomes a calvin_atom: `pattern@78.2+/-2.1`. Next time this pattern appears, the system recognizes it instantly. This IS the "memory leak precursor" discovery — no human programmed it. |
| **Boyer** | `speed > rw` → EXECUTE (alert). One reading won't trigger. Sustained anomaly will. Motor strength decays for services that return to normal — alert fatigue eliminated. |

**What the system learns over time:**
- Motor memory strengthens for services with repeating issues (spaced repetition)
- Motor memory decays for services that self-heal (noise elimination)
- Calvin atoms capture patterns: "disk 90% + Tuesday 2am = backup job, not alert"
- Boyer threshold prevents premature alerting — evidence must be sufficient

### Use Case 2: Autonomous DevOps (SWE-bench Pattern)

**Problem:** LLM agents scan entire codebases wastefully. No memory of previous fixes.

**How CSOS solves it:**

```bash
# Agent reads a file → immediately absorb
csos-core command="cat src/auth/handler.go" substrate=src_auth
# Response: {"motor_strength": 0.82, "decision": "EXECUTE"}
# Motor says: "you understand src/auth well"

csos-core command="cat tests/auth_test.go" substrate=tests_auth
# Response: {"motor_strength": 0.02, "decision": "EXPLORE"}
# Motor says: "you haven't read tests/ — go here first"
```

**Physics at work:**

| Equation | What It Does in This Context |
|----------|------------------------------|
| **Motor memory** | Tracks per-file familiarity via spaced repetition. `src/auth` read 5 times → strength 0.82. `tests/` never read → strength 0.02. Agent knows WHERE to look without scanning everything. |
| **Chain Calvin** | Records successful fix sequences: "read error_log → read handler.go → edit line 42 → tests pass". After 3 similar fixes, this chain becomes a calvin_atom — the agent has institutional knowledge of the fix pattern. |
| **Boyer** | Gates code changes. Agent won't push a fix until `speed > rw` (enough evidence from reading source + tests + error logs). Prevents premature edits. |
| **Forster** | Knowledge transfers across sessions. Yesterday's codebase understanding (gradient, motor memory, Calvin atoms) loads from `.csos/rings/` today. No cold start. |

### Use Case 3: Multi-Tool Orchestration

**Problem:** LLMs lose context in 10+ step workflows. Step 15 contradicts step 3.

**How CSOS solves it:**

```bash
# 15-step incident response workflow
for step in "check_k8s" "read_logs" "query_db" "check_dns" "ping_upstream" \
            "read_config" "check_certs" "trace_request" "check_memory" "read_metrics" \
            "compare_baseline" "check_deploys" "read_changelog" "verify_rollback" "confirm_fix"; do
  curl -X POST http://localhost:4200/api/command \
    -d "{\"action\":\"absorb\",\"substrate\":\"$step\",\"output\":\"step data for $step\"}"
done

# After 15 steps, check cumulative state
curl -X POST http://localhost:4200/api/command \
  -d '{"action":"see","ring":"eco_organism","detail":"cockpit"}'
# Returns: gradient=accumulated, speed=evidence_rate, motor_entries=15
# The membrane tracked ALL 15 steps. Nothing forgotten.
```

**Physics at work:**

| Equation | What It Does in This Context |
|----------|------------------------------|
| **Mitchell** | Gradient accumulates across ALL 15 steps. Step 1's evidence is still there at step 15. No context window overflow — the gradient IS the compressed memory. |
| **Motor memory** | Prioritizes which tools work. If `check_k8s` consistently yields `delta > 0` but `ping_upstream` yields `delta = 0`, motor strength reflects this. Next incident, the agent tries k8s first. |
| **Boyer** | Won't trigger remediation action until 15 steps build sufficient evidence. `speed > rw` at step 12 → EXECUTE the fix. At step 5 → EXPLORE (keep gathering). No premature action. |
| **Calvin** | Discovers tool sequences: "check_k8s → read_logs → check_memory → fix" becomes a chain_calvin atom. Next similar incident, the system suggests the proven sequence. |

### Ops Spec File

Create `specs/ops.csos` with infrastructure-specific atoms:

```bash
# Already created at specs/ops.csos
cat specs/ops.csos
```

The ops spec defines 5 new atoms (cpu_resonance, memory_pressure, latency_gate, error_rate, deploy_detector) that extend the base 5 equations for infrastructure monitoring. Load them by editing `specs/eco.csos` or by creating a new ring via the `grow` action.

### Monitoring Dashboard

```bash
# Start daemon
./csos --http 4200

# Open canvas: http://localhost:4200
# The canvas shows:
#   - Ring gauges (gradient, speed, decision per compartment)
#   - Timeline (every absorb event with decision and delta)
#   - Templates (select "Data Pipeline" for metric ingestion)
#   - SSE live updates (no polling — EventSource connection)
```

### Watchdog Integration

```bash
# Health check every 15 minutes
crontab -e
*/15 * * * * /path/to/scripts/watchdog.sh

# The watchdog:
# 1. Runs diagnose via native binary
# 2. If degraded → feeds stabilization signal
# 3. Saves state to disk
# 4. Logs to .csos/watchdog.log
```

---

## License

MIT
