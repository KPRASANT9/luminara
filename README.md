# CSOS — Complex Systems Operating System

A physics-grounded autonomous agent framework. Five photosynthetic equations, compiled to native code, operating on any substrate at any clock speed through a 4-layer architecture built on B-Tree storage, B+ Tree indexing, LLVM-compiled physics, and unified local/RDMA transport.

---

## What It Does

CSOS absorbs high-entropy input from the world and guides humans to minimum-entropy decisions through physics. The system observes signals, matches them against five governing equations (Gouterman, Marcus, Mitchell, Forster, Boyer), and autonomously decides whether to deliver a result or gather more evidence.

```
HIGH ENTROPY     "Help me land a job"              (infinite possibilities)
      | observe -> absorb -> physics says EXPLORE
MEDIUM           "700 developer jobs found"         (still too broad)
      | gradient stagnates -> ask ONE question
      | human: "Senior Android, 180-220k"
NARROWING        "23 matching roles in Hyderabad"   (focused)
      | observe -> absorb -> physics says EXECUTE
MINIMUM ENTROPY  "Top 5 roles. Apply to #1 and #3?" (actionable)
```

---

## Architecture

### Current (v12 — Python reference implementation)

```
User -> OpenCode TUI -> Agent (.md) -> csos-core.ts -> stdin/stdout pipe
                                              |
                                    csos-daemon.py (Python)
                                              |
                                    core.py (5 equations)
                                              |
                                    .csos/rings/*.json
```

One daemon process, one Core() in memory, three ecosystem rings. Agents call `csos-core` which pipes JSON to the Python daemon. Every CLI command and web fetch auto-absorbs through the 3-ring membrane. The agent receives data AND the physics decision in one response.

### Target (v13 — Native 4-layer framework)

```
User -> L4 Gateway (HTTP/WS/gRPC/CLI/Cron) -> L3 Compute (LLVM compiled)
                                                      |
              L2 Transport (local ring buffer / RDMA) -+
                                                      |
                               L1 Store (B-Tree + B+ Index + WAL)
```

Single native binary. Four data structures (Page, Record, Ring, Index) used by every layer. Physics and agent logic compiled into one LLVM module. Zero serialization between layers. Crash-safe via write-ahead log.

```
+------------------------------------------------------------------+
|                   csos (single binary, ~12K LOC)                  |
|                                                                    |
|  SHARED: lib/page.h  lib/record.h  lib/ring.h  lib/index.h       |
|          (4 universal data structures)                             |
|                                                                    |
|  +--------------------------------------------------------------+ |
|  | L4: GATEWAY                                                   | |
|  | Protocols: STDIO, HTTP, WebSocket, gRPC, CLI, Cron, Webhook  | |
|  | Uses: Record (IORequest/IOResponse), Ring (connection pools)  | |
|  +--------------------------------------------------------------+ |
|  | L3: COMPUTE                                                   | |
|  | Agent: plan + build + cross-living (one state machine)        | |
|  | Physics: fly, calvin, diffuse, decide (LLVM compiled)         | |
|  | Uses: Record (Photon/Atom/Agent), Ring (PhysicsRing)          | |
|  | Agent calls physics = direct function call (same LLVM module) | |
|  +--------------------------------------------------------------+ |
|  | L2: TRANSPORT                                                 | |
|  | Local: Ring(buffer=mmap'd heap)                               | |
|  | Remote: Ring(buffer=RDMA memory region)                       | |
|  | Fallback: Ring(buffer=TCP socket)                             | |
|  | One ring_push/ring_pop API for all modes                      | |
|  +--------------------------------------------------------------+ |
|  | L1: STORE                                                     | |
|  | Storage: Index(leaf_chain=false) -- B-Tree for data pages     | |
|  | Meta:    Index(leaf_chain=true)  -- B+ Tree for indexed lookup| |
|  | WAL:     Ring(buffer=mmap'd file) -- crash recovery log       | |
|  +--------------------------------------------------------------+ |
+------------------------------------------------------------------+
```

---

## The 4 Universal Data Structures

Every component in the system is built from these four structures. Nothing else exists.

| Structure | What It Is | Used By |
|-----------|-----------|---------|
| **Page** | Fixed 4096-byte container with header + typed payload | B-Tree nodes, B+ Tree nodes, RDMA transfer units, ring state snapshots |
| **Record** | Variable-size typed data within a Page | Photon (21 bytes), Atom, Session field, IORequest, IOResponse, Agent state |
| **Ring** | Circular buffer with atomic read/write positions | Physics rings, message bus, RDMA queue pairs, WAL, f_history, observation buffers |
| **Index** | Sorted key-to-page_id mapping with tree traversal | B-Tree (leaf_chain=false) for storage, B+ Tree (leaf_chain=true) for indexed queries |

### Physics-to-data-structure isomorphism

The CSOS physics concepts and the systems data structures are the same mathematics:

| Physics Concept | Data Structure | Same Math |
|----------------|---------------|-----------|
| Ring.gradient | ring_depth (write_pos - read_pos) | Count of accumulated events |
| Ring.speed | ring_throughput (gradient / window) | Rate of accumulation |
| Atom.photons | Ring of PhotonRecords | Bounded event log |
| diffuse(src, tgt) | RDMA page transfer | Zero-copy block move |
| Boyer decision | ring_speed > threshold | Throughput exceeds stability |
| Calvin synthesis | Index.put(new_atom) | B-Tree insert |

The data structure IS the physics. `ring_depth()` IS gradient. No separate computation layer needed.

---

## The 5 Equations

| # | Equation | Photosynthesis | Decision Role |
|---|----------|----------------|---------------|
| 1 | **Gouterman 1961** `dE=hc/l` | Photon absorption spectrum | Does this signal match what we know? |
| 2 | **Marcus 1956** `k=exp(-(dG+l)^2/4lkT)` | Electron transfer barriers | How far off is reality from prediction? |
| 3 | **Mitchell 1961** `dG=-nFdy+2.3RT*dpH` | Proton gradient storage | How much evidence do we have? |
| 4 | **Forster 1948** `k_ET=(1/t)(R0/r)^6` | Resonance energy transfer | Can knowledge from one domain help another? |
| 5 | **Boyer 1997** `ATP=flux*n/3` | ATP synthase motor | Is evidence sufficient for a decision? |

All five are implemented as Atoms within Rings. Each Atom predicts, observes, measures error, and self-tunes. The Calvin cycle synthesizes new Atoms from non-resonated signals (patterns the system has never seen).

---

## The 3 Rings

| Ring | Layer | Purpose |
|------|-------|---------|
| **eco_domain** | Substrate signals | Absorbs all external signals. Calvin atoms grow per substrate. 5 equation atoms as foundation. |
| **eco_cockpit** | Agent wisdom | Tracks specificity_delta, action_ratio, calvin_rate, boundary_crossings. Flow metrics, not telemetry. |
| **eco_organism** | Integration | Aggregates domain + cockpit. `speed > rw` triggers Boyer decision: EXECUTE or EXPLORE. |

---

## Agents: plan + build + cross-living

Two modes. One state machine. Physics always validated.

| Agent | Mode | Capabilities | When to use |
|-------|------|-------------|-------------|
| **plan** | Read-only observation | read, grep, glob, web, exec (via csos-core) | "Investigate X", "What is Y", "Analyze Z" |
| **build** | Observation + delivery | All plan capabilities + write deliverables (.md/.csv/.docx) | "Write me a resume", "Create a report", "Build a tracker" |

**cross-living** is not a separate agent. It is the compiled transition function that switches between plan and build based on the Boyer decision gate:

```
plan mode: observe -> absorb -> physics says EXECUTE? -> switch to build
build mode: deliver -> post-absorb -> delta > 0? DONE : switch back to plan
```

Physics validation (action_ratio, delta, gradient checks) is structural in the native runtime. Every operation passes through the 5 equations. There is no "non-physics" code path.

### The Entropy Reduction Loop

```
STEP 1: OBSERVE -- call ONE tool
   csos-core command="..." substrate=X    (CLI)
   csos-core url="..." substrate=X        (web)
   csos-core substrate=X output="..."     (bridge from read/grep)

STEP 2: READ THE PHYSICS
   Response contains: {physics: {decision, delta, organism: {speed, rw}}}
   
   decision = EXECUTE?     -> deliver result. DONE.
   decision = EXPLORE?     -> go to STEP 3.

STEP 3: CHECK GRADIENT HEALTH
   csos-core ring=eco_organism detail=cockpit
   
   action_ratio > 0.3 AND delta > 0?   -> observe more (STEP 1)
   action_ratio < 0.3 OR delta = 0?    -> ask human (STEP 4)
   speed > rw?                          -> deliver (confident enough)

STEP 4: ASK THE HUMAN (ONE question)
   Target the MISSING signal that tools cannot find.
   Store answer: csos-core key=<field> value=<answer>
   Return to STEP 1 with new information.
```

### Autonomous (headless) mode

When no human is present (cron, server), STEP 4 stores the question:

```
csos-core key=pending_question value="What role type?"
csos-core key=pending_substrate value="job_search"
-> continue with OTHER substrates that are not stuck
-> on next interactive session: surface pending questions first
```

---

## Data Segmentation

Every piece of data has ONE canonical location. The B+ Index provides multiple access paths to the SAME underlying B-Tree page. Indexes are derived. Only L1 pages are authoritative.

### Key Schema

```
{segment}:{substrate}:{entity}:{id}

Segments:
  ring:     Ring metadata (name, cycles, signals, F)
  atom:     Atom definitions (formula, params, limits)
  photon:   Signal events (predicted, actual, error, resonated)
  session:  Human data, cookies, auth state
  output:   Deliverables (reports, CSVs, documents)
  pending:  Queued questions for human (autonomous mode)
  spec:     Substrate definitions (.csos spec files)
  metric:   Cockpit metrics time-series

Examples:
  ring:eco_domain                         -> Ring header
  atom:eco_domain:gouterman               -> Atom definition
  photon:eco_domain:gouterman:0042        -> Single photon at cycle 42
  session:human:target_role               -> "Senior Android developer"
  session:alpha_price:cookies             -> Cookie jar
  pending:aws_cost:question               -> "Which cost tag?"
```

### No-duplication guarantees

1. **B-Tree structural uniqueness**: Cannot insert duplicate key. Same atom + same ring = same page. Update-in-place, never second copy.
2. **Content-addressed overflow pages**: Large values (web responses, CLI output) stored in pages keyed by SHA256(content). Same content at two timestamps = stored ONCE, referenced TWICE.
3. **Indexes are pointers, not copies**: B+ idx_gradient and B+ idx_primary both point to the same L1 page. One copy of data, many access paths.
4. **Concurrent safety**: B+ Tree page-level operations. Two agents writing different session keys = no conflict. No read-entire-file/write-entire-file pattern.

---

## L2 Transport -- The Muscle Memory Layer

The L2 Transport layer sits between L4 Gateway and L3 Compute. It is the system's MUSCLE MEMORY -- a ring-buffer-based message bus that learns which operations matter through spaced repetition.

### Transport Modes

| Mode | Backing | Use Case | Command |
|------|---------|----------|---------|
| **DIRECT** | In-process function call | Default CLI daemon, zero overhead | `./csos-native` |
| **MMAP** | mmap'd shared memory file | Cross-process IPC on same machine | (programmatic) |
| **UNIX** | Unix domain socket | Local network daemon, any process | `./csos-native --unix /tmp/csos.sock` |
| **TCP** | TCP socket | Cross-machine communication | (future) |

All modes use the same `csos_ring_t` data structure. Same `ring_push`/`ring_pop` API. The transport mode only affects where the ring buffer's backing memory lives.

### Muscle Memory via Spaced Repetition

Every ingress signal passes through the transport layer, which records a "motor trace" -- a sliding window of substrate access patterns. The system uses spaced repetition principles from cognitive science to build muscle memory:

**How it works:**

1. Each substrate access records: hash, timestamp, interval since last access
2. If interval is INCREASING but signal still appears -> spaced repetition succeeding -> strengthen motor memory (boost substrate priority)
3. If interval is DECREASING -> cramming, not learning -> smaller boost
4. If signal STOPS appearing -> decay (strength decreases over time)
5. One-shot signals get minimal strength (0.1)

**Why this matters for the system:**

| Motor Pattern | What System Learns | Effect |
|--------------|-------------------|--------|
| Substrate checked at growing intervals | This substrate is reliably important | Higher priority in agent observation queue |
| Substrate crammed (every cycle) | Short-term urgency, not long-term value | Moderate priority, may decay |
| Substrate accessed once, never again | Noise, not signal | Low priority, evicted from trace |
| Substrate accessed late but regularly | Emerging importance | Growing priority |

**Spaced repetition applied to CSOS operations:**

- Physics rings already implement spaced repetition via Gouterman: signals that recur at increasing intervals get STRONGER resonance (gradient grows). Signals that stop recurring naturally DECAY (local_photons age out).
- The transport muscle memory adds OPERATIONAL spaced repetition: which tools, URLs, commands, and substrates to prioritize.
- Agent `choose_observation()` can query `muscle_top()` to prioritize substrates by motor strength instead of scanning randomly.

```bash
# View muscle memory state
./csos-native --muscle

# Example output:
# {
#   "muscle_entries": 5,
#   "global_cycle": 155,
#   "traces": [
#     {"hash": 3003, "reps": 6, "strength": 0.712, "interval": 39, "spaced": true},
#     {"hash": 1001, "reps": 100, "strength": 1.000, "interval": 2, "spaced": false},
#     {"hash": 2002, "reps": 20, "strength": 0.837, "interval": 11, "spaced": false}
#   ]
# }
# Note: hash 3003 has "spaced": true -- interval is growing, muscle memory strengthening
```

### Ingress/Egress Ring Architecture

```
INGRESS (external -> compute):
  L4 Gateway -> transport.ingress ring -> L3 Compute
                      |
               muscle_trace[] records substrate access pattern
               spaced repetition scoring updates motor strength

EGRESS (compute -> external):
  L3 Compute -> transport.egress ring -> L4 Gateway
                      |
               subscriber table notifies protocol handlers
               auto-absorb: egress signals feed back into ingress

FEEDBACK LOOP:
  Every egress response gets absorbed back into the ingress ring.
  The system learns from its own outputs. If an HTTP request to
  URL X consistently produces high-delta signals, the muscle memory
  for X's substrate strengthens. If URL Y produces zero-delta,
  its muscle memory decays. The system self-optimizes which
  external touchpoints are worth probing.
```

---

## Ingress / Egress

### Ingress (how data enters)

| Entry Point | Protocol | Layer | Status | Command |
|-------------|----------|-------|--------|---------|
| **Terminal** | STDIO | L4 Gateway | Implemented | `./csos-native` (default) |
| **CLI one-shot** | CLI pipe | L4 Gateway | Implemented | `echo '{"action":"absorb",...}' \| ./csos-native` |
| **Unix Socket** | UNIX | L2 Transport | Implemented | `./csos-native --unix /tmp/csos.sock` |
| **HTTP API** | REST | L2 Transport | Implemented | `./csos-native --http 4096` |
| **OpenCode Agent** | Tool call | L4 Gateway | Implemented | `opencode > @plan investigate X` |
| **WebSocket** | WS | L2 Transport | Planned | Bidirectional push for events |
| **gRPC** | Protobuf | L2 Transport | Planned | Structured RPC, streaming |
| **Webhooks** | HTTP POST | L4 Gateway | Planned | Auto-route GitHub/Slack events |
| **Cron** | Timer | L4 Gateway | Planned | `./csos-native --cron 15m absorb` |
| **Browser** | Playwright/CDP | L4 Gateway | Via Python | `webautomate url="..."` |

### Egress (how data exits)

| Trigger | Output | Format | Through |
|---------|--------|--------|---------|
| Agent EXECUTE decision | Deliverable to user | .md, .csv, .txt, .docx | L2 egress ring -> L4 protocol |
| Agent ASK decision | ONE clarifying question | Text message | L2 egress ring -> L4 STDIO/HTTP/WS |
| Agent STORE (autonomous) | Pending question | Saved to session | L2 egress ring -> L1 Store |
| Physics event | Gradient broadcast | JSON metrics | L2 egress ring -> subscribers |
| Anomaly detection | Alert | Calvin atom + notification | L2 egress ring -> webhook |
| Cross-substrate coupling | Forster diffuse | Page transfer | L2 transport ring (local or RDMA) |
| Muscle memory update | Priority change | Motor trace | L2 muscle_trace[] |

### Auto-absorb on egress

Every outbound operation feeds back through the ingress ring. When the system sends an HTTP request, the response is absorbed. When it writes a file, the file metadata is absorbed. The transport layer records each egress as a motor operation, building muscle memory for which outputs produce useful feedback.

### Ingress-to-Egress data flow per layer

```
Layer  Ingress Does                           Egress Does
-----  -----------------------------------   -----------------------------------
L4     Parse protocol (HTTP/STDIO/WS)        Format protocol (HTTP response/print)
       Extract JSON body                      Write deliverables (.md/.csv)
       Route to action handler                Send alerts/notifications
       Law enforcement (reject build cmds)    Auto-absorb output metadata

L2     Push to ingress ring                   Pop from egress ring
       Record muscle trace (motor memory)     Notify subscribers
       Spaced repetition scoring              Feed back to ingress (auto-absorb)
       mmap/socket for cross-process          Gradient broadcast to remote nodes

L3     Absorb signal -> fly 3 rings           Physics result -> JSON response
       Calvin synthesis                       Agent decision (EXECUTE/EXPLORE/ASK)
       Forster coupling                       Cross-living mode transition
       Boyer decision gate                    Pending question storage

L1     Write photon pages (WAL first)         Read pages for see/cockpit queries
       Update B+ indexes                      Range scan for history queries
       Checkpoint WAL                         Export JSON (backward compat)
```

---

## Error Resilience

Errors are signals, not failures. Every error becomes a non-resonated photon and feeds the Calvin synthesis cycle. The system learns from errors.

| Layer | Error Type | Handling |
|-------|-----------|---------|
| L1 Store | Page corruption | WAL replay to last consistent state |
| L1 Store | Disk full | WAL compaction + alert |
| L1 Store | B-Tree imbalance | Auto-rebalance on next split |
| L2 Transport | RDMA disconnect | Degrade to local-only (mmap fallback) |
| L2 Transport | Timeout | Forster coupling set to infinity (skip remote) |
| L3 Compute | LLVM compile error | Fall back to interpreted Python reference |
| L3 Compute | Calvin JIT failure | Keep previous compiled version |
| L4 Gateway | CLI command fails | absorb(stderr) as signal, not crash |
| L4 Gateway | URL 404/500 | absorb(status_code), system learns URL unreliable |
| L4 Gateway | Auth expired | Retry once with refreshed cookies, then ask human |

---

## Multi-Clock Substrates

Different substrates operate at different temporal resolutions. The Boyer equation encodes clock speed: `ATP = flux * n / 3` where flux = observation frequency.

| Substrate Type | Clock Speed | Example |
|---------------|-------------|---------|
| Market tick data | 100ms - 1s | Price microstructure |
| Infrastructure monitoring | 1s - 5min | CPU, memory, latency |
| Cost tracking | 1hr - 1day | Cloud billing |
| Hiring pipeline | 1day | LinkedIn, Greenhouse |
| Geological/climate | hours - years | Long-term measurements |
| Particle physics | femtoseconds | Collision events |

Adaptive clocking: if gradient is accelerating (F decreasing), frequency increases. If gradient is stable, frequency decreases. The physics self-tunes observation rate.

---

## Use Cases

### Institutional Memory
Human answers, observations, and Calvin-synthesized patterns persist in L1 B-Tree pages across years. Range queries via B+ index: "all decisions about payment gateway since Q3 2025" returns in milliseconds.

### Market Sensing
Continuous signal absorption at substrate-native clock speeds. Cross-substrate Forster coupling: VIX spike atoms transfer to SPY ring, system learns correlations without manual configuration.

### Predictive Ops
Calvin atoms encode patterns (e.g., "CPU at 78% without deploy = memory leak precursor"). Gouterman resonance triggers on pattern match BEFORE failure occurs. Cross-substrate coupling correlates infra + deploy + cost signals.

### Cross-Domain Insight
Forster diffuse transfers knowledge between unrelated substrates. Hiring patterns inform cloud cost predictions. NPS scores couple with feature flags. Each successful transfer increments boundary_crossings toward civilizational scale.

### Autonomous Enterprise
Multiple substrates at independent clock speeds. Cron-triggered autonomous operation. Pending questions queued for human. Daily briefings from physics state. No human intervention required for routine monitoring.

---

## Project Structure

### Current (v12)

```
V12/
+-- src/
|   +-- core/
|       +-- core.py                 400 LOC  5 equations (reference implementation)
+-- scripts/
|   +-- csos-daemon.py              281 LOC  The chloroplast (Python daemon)
|   +-- csos-login.py                        Browser authentication
|   +-- watchdog.sh                 141 LOC  Health monitor + auto-correct
+-- .opencode/
|   +-- opencode.json                        Agent wiring + permissions
|   +-- tools/
|   |   +-- csos-core.ts            89 LOC  Pure pipe to daemon
|   |   +-- webautomate.ts          63 LOC  Login automation
|   +-- agents/
|   |   +-- plan.md                          Observe + answer
|   |   +-- build.md                         Deliver ONE thing
|   |   +-- csos-living.md                   Full entropy loop
|   +-- skills/
|       +-- csos-core/                       Physics engine reference
|       +-- bridge/                          Agent state sharing
+-- .csos/
|   +-- rings/
|   |   +-- eco_domain.json                  Substrate signals + Calvin atoms
|   |   +-- eco_cockpit.json                 Agent wisdom metrics
|   |   +-- eco_organism.json                Integration + Boyer decisions
|   +-- sessions/
|       +-- human.json                       Human data + auth + pending questions
+-- AGENTS.md                                Guidance loop + 3 Laws
+-- Dockerfile                               Container runtime
+-- docker-compose.yml                       Production stack
+-- pyproject.toml                           Python config
+-- package.json                             Node.js deps (Playwright)
```

### Target (v13)

```
V13/
+-- lib/                             182 LOC  4 universal data structures
|   +-- page.h                                Fixed 4096-byte container
|   +-- record.h                              Variable-size typed data
|   +-- ring.h                                Circular buffer with atomics
|   +-- index.h                               B-Tree / B+ Tree (one bool flag)
+-- src/
|   +-- store/                      ~3K LOC   L1: persistence + indexing
|   |   +-- page.c                            Page read/write/checksum
|   |   +-- index.c                           B-Tree AND B+ Tree (leaf_chain flag)
|   |   +-- wal.c                             Write-ahead log (Ring-backed)
|   |   +-- store.c                           Unified storage API
|   +-- transport/                  ~2K LOC   L2: local bus + RDMA
|   |   +-- ring.c                            Universal ring buffer operations
|   |   +-- local.c                           mmap-backed ring (same-process)
|   |   +-- rdma.c                            RDMA-backed ring (cross-node)
|   |   +-- tcp.c                             TCP fallback (no RDMA hardware)
|   +-- compute/                    ~4K LOC   L3: agents + physics (one LLVM module)
|   |   +-- lexer.c                           .csos spec tokenizer
|   |   +-- parser.c                          AST builder
|   |   +-- codegen.cpp                       LLVM IR generation
|   |   +-- jit.cpp                           JIT compile + Calvin hot-reload
|   |   +-- agent.c                           plan + build + cross-living state machine
|   |   +-- physics.c                         fly, calvin, diffuse, decide runtime
|   +-- gateway/                    ~3K LOC   L4: all external I/O
|   |   +-- http.c                            HTTP server + client
|   |   +-- websocket.c                       Bidirectional streaming
|   |   +-- grpc.c                            Structured RPC
|   |   +-- stdio.c                           Terminal interactive mode
|   |   +-- cli.c                             One-shot command mode
|   |   +-- browser.c                         Playwright/CDP integration
|   |   +-- cron.c                            Scheduled autonomous triggers
|   +-- core/
|       +-- core.py                           Reference implementation (kept for spec)
+-- specs/
|   +-- eco.csos                              5 equations + agent definitions
|   +-- market.csos                           Market substrate spec (example)
+-- scripts/
|   +-- watchdog.sh                           Health monitor (calls native binary)
+-- .opencode/
|   +-- opencode.json                         Agent wiring (plan + build only)
|   +-- tools/
|   |   +-- csos-core.ts                      Pipe to native daemon (Phase 1)
|   |   +-- webautomate.ts                    Login automation
|   +-- agents/
|       +-- plan.md                           Observe + answer (delegates to native)
|       +-- build.md                          Deliver ONE thing (delegates to native)
+-- .csos/
|   +-- data.db                               L1 B-Tree file (replaces rings/*.json)
|   +-- meta.idx                              L2 B+ indexes
|   +-- wal.log                               Write-ahead log
|   +-- sessions/                             Legacy compatibility
+-- Makefile                                  Build: L1 -> L2 -> L3 -> L4 -> binary
+-- AGENTS.md                                 Guidance loop + 3 Laws
+-- Dockerfile                                Multi-stage: build native + runtime
+-- docker-compose.yml                        Production stack
```

---

## Build & Compile

### Prerequisites

```bash
# macOS
brew install llvm protobuf
export PATH="/usr/local/opt/llvm/bin:$PATH"
export LDFLAGS="-L/usr/local/opt/llvm/lib"
export CPPFLAGS="-I/usr/local/opt/llvm/include"

# Linux (Ubuntu/Debian)
apt-get install llvm-18-dev libprotobuf-dev protobuf-compiler \
    libibverbs-dev librdmacm-dev    # RDMA (optional, Linux only)

# Both platforms
pip install requests playwright && playwright install chromium
npm i -g opencode-ai
```

### Build the native binary

```bash
make all        # Build everything: L1 -> L2 -> L3 -> L4 -> csos binary
make test       # Run test suite
make bench      # Benchmark: fly() latency, B-Tree ops/sec, ring throughput
```

### Makefile targets

```makefile
# Phase 1: L1 Store (B-Tree + B+ Index + WAL)
lib-store:
    cc -O2 -o build/store.o -c src/store/page.c src/store/index.c \
       src/store/wal.c src/store/store.c -Ilib

# Phase 2: L2 Transport (Ring buffer, local + RDMA + TCP)
lib-transport:
    cc -O2 -o build/transport.o -c src/transport/ring.c \
       src/transport/local.c src/transport/rdma.c src/transport/tcp.c -Ilib

# Phase 3: L3 Compute (LLVM-compiled physics + agents)
lib-compute:
    cc -O2 -c src/compute/lexer.c src/compute/parser.c \
       src/compute/agent.c src/compute/physics.c -Ilib -o build/compute_c.o
    c++ -O2 -c src/compute/codegen.cpp src/compute/jit.cpp \
       -Ilib $(shell llvm-config --cxxflags) -o build/compute_cpp.o

# Phase 4: L4 Gateway (HTTP, WebSocket, gRPC, CLI, Cron)
lib-gateway:
    cc -O2 -o build/gateway.o -c src/gateway/http.c src/gateway/websocket.c \
       src/gateway/grpc.c src/gateway/stdio.c src/gateway/cli.c \
       src/gateway/browser.c src/gateway/cron.c -Ilib

# Link everything into single binary
csos: lib-store lib-transport lib-compute lib-gateway
    c++ -O2 -o csos build/*.o $(shell llvm-config --ldflags --libs core) \
       -lcurl -lprotobuf -lpthread

# Install
install: csos
    cp csos /usr/local/bin/csos

# Test
test: csos
    ./csos test --suite=store      # B-Tree CRUD, WAL recovery, B+ range scans
    ./csos test --suite=transport   # Ring push/pop, local/TCP throughput
    ./csos test --suite=compute     # fly() correctness, Calvin synthesis, Boyer gate
    ./csos test --suite=gateway     # HTTP round-trip, CLI exec, cron scheduling

# Benchmark
bench: csos
    ./csos bench --op=fly --atoms=5 --signals=20        # Target: <10us
    ./csos bench --op=btree --ops=100000                 # Target: >500K ops/sec
    ./csos bench --op=ring --producers=4 --consumers=4   # Target: >1M msg/sec
```

### Build output

```
build/
+-- store.o          ~30KB   L1: B-Tree + B+ Index + WAL
+-- transport.o      ~20KB   L2: Ring buffer (local/RDMA/TCP)
+-- compute_c.o      ~25KB   L3: Agent state machine + physics runtime
+-- compute_cpp.o    ~40KB   L3: LLVM codegen + JIT
+-- gateway.o        ~35KB   L4: All protocol handlers
+-- csos             ~2MB    Final linked binary (static, no runtime deps)
```

### Docker build (multi-stage)

```dockerfile
# Stage 1: Build native binary
FROM ubuntu:24.04 AS builder
RUN apt-get update && apt-get install -y build-essential llvm-18-dev \
    libcurl4-openssl-dev libprotobuf-dev libibverbs-dev
COPY lib/ lib/
COPY src/ src/
COPY Makefile .
RUN make csos

# Stage 2: Runtime
FROM ubuntu:24.04
RUN apt-get update && apt-get install -y python3 python3-pip nodejs npm \
    libcurl4 libprotobuf32 libibverbs1
COPY --from=builder /csos /usr/local/bin/csos
COPY .opencode/ .opencode/
COPY specs/ specs/
COPY scripts/watchdog.sh scripts/
RUN npm i -g opencode-ai && pip3 install requests playwright
EXPOSE 4096
CMD ["csos", "serve", "--port", "4096"]
```

---

## How Users Interact

### Phase 1: Drop-in replacement (transparent)

OpenCode agents work exactly as before. One line changes in csos-core.ts:

```typescript
// BEFORE:
spawn("python3", ["scripts/csos-daemon.py"])

// AFTER:
spawn("./csos", ["daemon"])
```

Same stdin/stdout JSON protocol. Same agent .md files. Same `csos-core` tool interface. User sees: faster responses, larger substrates, crash-safe data. Nothing else changes.

```bash
# Exactly the same commands as today
opencode
> @plan What's our API latency trend this week?
> @build Write me a Q1 performance summary
```

### Phase 2: Native agent delegation (hybrid)

Agent .md files delegate mechanical steps to the native runtime. LLM is called only for judgment (what to observe, how to phrase questions, how to compose deliverables).

```bash
opencode
> @plan Investigate why cloud costs spiked

# Agent internally:
#   LLM decides: "I should check AWS cost explorer"
#   csos-core action=agent_step command="aws ce get-cost..." substrate=aws_cost
#   Native runtime: absorb -> fly -> calvin -> check physics -> loop
#   Returns to LLM only when: EXECUTE (compose answer) or STUCK (compose question)
#   Result: 4 LLM calls instead of 20
```

### Phase 3: Full native with multiple frontends

The native binary is the system. OpenCode becomes one of many possible frontends.

```bash
# Terminal interactive (same as today)
csos tui
> help me find senior Android developer roles

# CLI one-shot
csos exec "kubectl get pods -l app=api" --substrate=k8s
csos query "ring:eco_organism:cockpit"
csos absorb --substrate=sales_crm < quarterly_data.csv

# HTTP API
curl -X POST http://localhost:4096/api/absorb \
  -H "Content-Type: application/json" \
  -d '{"substrate": "market_spy", "data": "SPY 523.41 +0.8%"}'

# WebSocket (real-time agent interaction)
wscat -c ws://localhost:4096/agent/plan
> {"task": "analyze API performance"}
< {"type": "observation", "data": "fetching metrics..."}
< {"type": "question", "text": "Which service: api-gateway or api-core?"}
> {"answer": "api-gateway"}
< {"type": "deliverable", "content": "API gateway p99 latency..."}

# gRPC (programmatic integration)
grpcurl -d '{"substrate":"infra_cpu","data":"cpu=78.3%"}' \
  localhost:4096 csos.CSOS/Absorb

# Cron (autonomous operation)
csos schedule --every=15m --agent=plan --task="absorb market signals"
csos schedule --every=1h  --agent=plan --task="check infrastructure"
csos schedule --every=24h --agent=build --task="daily briefing"

# Webhooks (external event sources)
# Configure in specs/webhooks.csos:
#   webhook github -> substrate=eng_velocity
#   webhook stripe -> substrate=revenue
#   webhook slack  -> substrate=team_sentiment
```

---

## Implementation Roadmap

### Phase 1: L1 Store (B-Tree + B+ Index + WAL)

Replace `json.dumps`/`json.loads`/`write_text`/`read_text` with page-level storage.

- Write: O(log n) per photon instead of O(n) per ring
- Read: B+ range scan instead of load-everything-into-memory
- Safety: WAL ensures no data loss on crash
- Test: Verify eco_domain ring survives kill -9 + restart

Deliverable: Python C extension. `core.py` _save()/_load() call L1 natively. JSON files eliminated. Everything else unchanged.

### Phase 2: L2 Transport (Ring buffer)

Replace stdin/stdout JSON pipe with mmap'd ring buffer.

- Local: Agent and daemon communicate via shared memory ring
- Zero serialization for same-machine components
- Same ring buffer protocol used for RDMA when Phase 5 adds it

Deliverable: csos-core.ts spawns native daemon, communicates via unix socket instead of stdin pipe.

### Phase 3: L3 Compute (LLVM physics + agent)

Compile the 5 equations and agent state machine to native code.

- fly(): ~10us instead of ~500us (50x speedup)
- Calvin synthesis with JIT hot-reload
- Agent transitions compiled (plan/build/cross-living)
- LLM called only for judgment (4 calls instead of 20 per task)

Deliverable: `specs/eco.csos` contains equations + agent definitions. LLVM compiles to native. Python core.py becomes reference-only.

### Phase 4: L4 Gateway (universal I/O)

Add HTTP, WebSocket, gRPC, cron, webhook handlers.

- All protocols map to unified IORequest/IOResponse (Record type)
- Auto-absorb on every egress
- Law enforcement at gateway level (forbidden patterns)

Deliverable: `csos serve --port 4096` replaces OpenCode server. Multiple frontends supported.

### Phase 5: RDMA + multi-node

Extend L2 Transport with RDMA memory regions.

- Ring buffer backed by RDMA MR instead of mmap
- Forster diffuse() becomes zero-copy page transfer
- Gradient broadcast to all remote peers
- TCP fallback when no RDMA hardware

Deliverable: Multiple CSOS instances form a substrate mesh. Each node runs substrates at its own clock speed. Forster coupling connects them.

---

## The 3 Laws

Enforced structurally. Not by convention — by architecture.

**I. No Hardcoded Logic.** All decisions flow from the 5 equations. The agent state machine has no `if domain == "finance"` branches. Boyer decides EXECUTE/EXPLORE. Gouterman decides what resonates. Marcus corrects errors. The physics is the logic.

**II. All State from .csos/.** One B-Tree file (data.db) + B+ indexes (meta.idx) + WAL (wal.log). No distributed config. No environment-specific state. Volume mount `.csos/` and the entire system state travels with it.

**III. New Substrates = Zero Code.** Write a `.csos` spec file. LLVM compiles it. B+ indexes it. B-Tree stores it. Transport distributes it. The 5 equations, compiled once, run on every substrate at every clock speed. Adding a substrate is adding a file, not adding code.

---

## Quick Start (Current v12)

```bash
# Install dependencies
npm i -g opencode-ai
pip install requests playwright && playwright install chromium

# Run
opencode
> @plan What is our current infrastructure health?
> @build Write a cost analysis report for March 2026
```

## Quick Start (Target v13)

```bash
# Build
brew install llvm protobuf          # macOS
make all                             # Compile native binary

# Run (any of these)
csos tui                             # Interactive terminal
csos serve --port 4096               # Server mode (HTTP + WS + gRPC)
csos exec "ls -la" --substrate=fs    # One-shot CLI
opencode                             # Via OpenCode (Phase 1 compatibility)
```

---

## License

MIT
