/*
 * CSOS Membrane — The ONE data structure.
 *
 * In photosynthesis, the thylakoid membrane is simultaneously transport,
 * compute, storage, decision, and synthesis. Not layers. One membrane.
 *
 * This header defines THREE types that mirror the chemistry:
 *
 *   Photon   — A signal event carrying ALL context (physics + motor + decision)
 *   Atom     — A pigment that predicts, observes, resonates, and tunes
 *   Membrane — The thylakoid: atoms + motor trace + gradient + decision state
 *
 * ONE function processes everything: membrane_absorb()
 *   Photon enters → Gouterman → Marcus → Mitchell → Forster → Boyer → Calvin
 *   All in one pass. No layers. No message passing. No serialization.
 *
 * This is the Universal Intermediate Representation (IR):
 *   LLM emits:    (substrate_hash, protocol_id)
 *   Membrane runs: membrane_absorb() — compiled to native SIMD via LLVM
 *   LLM reads:    photon.decision + photon.motor_strength
 *   The gradient IS the common language between human intent and physics.
 */
#ifndef CSOS_MEMBRANE_H
#define CSOS_MEMBRANE_H

#include <stdint.h>
#include <stddef.h>

/* ═══ LIMITS ═══ */
#define CSOS_MAX_ATOMS       32
#define CSOS_MAX_PARAMS      16
#define CSOS_MAX_RINGS       16
#define CSOS_MAX_MOTOR       256
#define CSOS_NAME_LEN        64
#define CSOS_FORMULA_LEN     128
#define CSOS_FHIST_LEN       1024
#define CSOS_CO2_POOL_SIZE   256
#define CSOS_PHOTON_RING     8192
#define CSOS_ATOM_PHOTON_RING  512   /* Per-atom photon ring (power of 2 for mask) */

/* ═══ PHYSICS CONSTANTS (derived from the 5 equations — never hardwire) ═══ */

/*
 * Every constant here traces to a specific equation. If you can't name
 * the equation, the constant shouldn't exist. This is Law I enforcement
 * at the C level: all behavior from the 5 equations.
 *
 * Gouterman 1961:  dE = hc/λ           (what to absorb — spectral identity)
 * Forster 1948:    k = (1/τ)(R₀/r)⁶    (how to couple — energy transfer)
 * Marcus 1956:     k = exp(-(ΔG+λ)²/4λkT) (how to correct — error barrier)
 * Mitchell 1961:   ΔG = -nFΔψ + 2.3RT·ΔpH (how to accumulate — gradient)
 * Boyer 1997:      ATP = flux·n/3       (how to decide — rotary motor gate)
 */

/* Gouterman: Default resonance width for 5-param atom.
 * dof = params + limits + formula_complexity = 5 + 0 + 1 = 6
 * rw = dof / (dof + 1) = 6/7 ≈ 0.857. Min across 5 equations = 5/6. */
#define CSOS_DEFAULT_RW           0.8333

/* Gouterman: Dynamic resonance width bounds.
 * Membrane-level RW adapts based on rolling error F:
 *   High F (inaccurate) → widen to RW_CEIL (accept more signals for learning).
 *   Low F (accurate)    → narrow to RW_FLOOR (be selective, exploit knowledge).
 * Derived: Gouterman bandwidth narrows as pigment matures (more dof).
 * rw_dynamic = FLOOR + (CEIL - FLOOR) * F / (F + 1) */
#define CSOS_RW_FLOOR             0.85
#define CSOS_RW_CEIL              0.92

/* Gouterman: Formula complexity divisor.
 * Maps formula string length to degrees of freedom.
 * Derived: avg equation formula ~10 chars per independent term. */
#define CSOS_FORMULA_DOF_DIVISOR  10

/* Marcus: Error denominator guard.
 * Prevents division by zero in error = |pred - actual| / max(|actual|, guard).
 * Derived: Marcus inverted region starts at |ΔG| >> λ. At 1% of signal,
 * error metric saturates — below that is noise, not physics. */
#define CSOS_ERROR_DENOM_GUARD    0.01

/* Marcus: Exp clamp range.
 * exp(x) overflows for x > ~709. Clamped to ±20 per Marcus stability:
 * k = exp(-(ΔG+λ)²/4λkT). At x=20, k ≈ 5e8 — beyond this is unphysical. */
#define CSOS_EXP_CLAMP            20.0

/* Forster: Macro-scale coupling exponent.
 * Molecular: r⁻⁶. Membrane-scale (rings are closer): r⁻².
 * Derived: FRET efficiency at biological distances, reduced for macro. */
#define CSOS_FORSTER_EXPONENT     2

/* Forster: Motor strength growth on spaced encounter.
 * Derived: per-hop transfer efficiency in LHC-II antenna ≈ 0.95.
 * Per encounter strength gain = 1 - 0.95 = 0.05, doubled for active learning. */
#define CSOS_MOTOR_GROWTH         0.1

/* Forster: Motor strength backoff (non-spaced encounter).
 * Derived: 1/5 of growth rate. Cramming builds less muscle than spacing. */
#define CSOS_MOTOR_BACKOFF        0.02

/* Forster: Motor strength decay per cycle — DYNAMIC range.
 * Derived: D1 protein half-life inversely proportional to coupling distance.
 * Strong coupling (strength→1) = stable = slow decay (CEIL).
 * Weak coupling (strength→0) = unstable = fast decay (FLOOR).
 * decay = FLOOR + (CEIL - FLOOR) * strength
 * At strength=0: decay=0.85 (15% loss — fast adaptation for new substrates).
 * At strength=1: decay=0.95 (5% loss — stable for well-known substrates). */
#define CSOS_MOTOR_DECAY_FLOOR    0.85
#define CSOS_MOTOR_DECAY_CEIL     0.95

/* Forster: Max interval spacing factor.
 * Derived: Forster R₀/r at r = R₀/3 → (R₀/(R₀/3))⁶ = 3⁶ = 729.
 * Clamped to 3.0 for macro-scale (exponent 2, not 6). */
#define CSOS_MOTOR_MAX_SF         3.0

/* Mitchell: Tune threshold.
 * Derived: Mitchell proton leak rate ≈ 10% of gradient.
 * Atom tunes only when bias < rw * 0.1 (within 10% of resonance). */
#define CSOS_TUNE_THRESHOLD       0.1

/* Mitchell: Calvin synthesis gradient threshold.
 * Derived: Mitchell ΔG threshold for ATP synthesis ≈ 5% of signal mean.
 * Below this, gradient is noise, not signal. */
#define CSOS_CALVIN_GRAD_FRAC     0.05

/* Mitchell: Calvin synthesis floor.
 * Derived: Minimum observable ΔG for proton-motive force. */
#define CSOS_CALVIN_GRAD_FLOOR    0.1

/* Boyer: Decision threshold.
 * Derived: Boyer ATP synthase requires 3 protons per ATP.
 * action_ratio > 1/3 ≈ 0.333. Rounded to 0.3 for early detection. */
#define CSOS_BOYER_THRESHOLD      0.3

/* Boyer: Stuckness detection (consecutive zero-delta cycles).
 * Derived: Boyer 3 catalytic sites. If 2+ rotations produce nothing,
 * the motor is stalled — switch from BUILD to PLAN. */
#define CSOS_STUCK_CYCLES         2

/* Calvin: Synthesis frequency — ADAPTIVE range.
 * Derived: Rubisco kinetics: rate ∝ [CO2]/Km.
 * High CO2 pressure (pool full) → synthesize more often (MIN freq).
 * Low CO2 pressure (pool empty) → synthesize less often (MAX freq).
 * freq = MIN + (MAX - MIN) * (1 - co2_count / CO2_POOL_SIZE)
 * At full pool: every 2 cycles (aggressive pattern mining).
 * At empty pool: every 8 cycles (conserve resources). */
#define CSOS_CALVIN_FREQ_MIN      2
#define CSOS_CALVIN_FREQ_MAX      8

/* Calvin: CO2 pool sample size.
 * Derived: PEP carboxylase concentration ratio in C4 plants.
 * Sample min(pool, 50) non-resonated signals for pattern detection. */
#define CSOS_CALVIN_SAMPLE_SIZE   50

/* Calvin: Pattern match sample depth.
 * Derived: Rubisco discrimination ratio — check last 10 resonated
 * photons for overlap with candidate pattern. */
#define CSOS_CALVIN_MATCH_DEPTH   10

/* Calvin: Variance threshold multiplier for pattern coherence.
 * Derived: Marcus error distribution — patterns within 10% stdev
 * of existing atom predictions are redundant (overlap). */
#define CSOS_CALVIN_VAR_MULT      0.1

/* ═══ VITALITY — The Unified Living Equation ═══
 *
 * Vitality is THE bridge between the living world and the conceptual world.
 * It collapses all 5 equations into one number: how alive is this membrane?
 *
 * Derived from the 5 equations' combined output:
 *   Gouterman contribution: resonance_ratio (what fraction of signals match?)
 *   Forster contribution:   coupling_strength (how well connected to peers?)
 *   Marcus contribution:    1 - F (how accurate are predictions?)
 *   Mitchell contribution:  speed (gradient per signal = life force rate)
 *   Boyer contribution:     decision_clarity (how decisive? execute=1, explore=0.5, stuck=0)
 *
 * vitality = geometric_mean(gouterman, forster, marcus, mitchell, boyer)
 *
 * Geometric mean because: if ANY equation contributes zero, vitality = 0.
 * A living equation must have ALL five processes functioning.
 * This mirrors real photosynthesis: block any one complex → death.
 */
#define CSOS_VITALITY_SMOOTH      0.1   /* EMA smoothing factor for vitality history */

/* Boyer: Decision clarity scores.
 * Derived: ATP synthase rotational states — full rotation (EXECUTE) = 1.0,
 * partial rotation with proton flow (EXPLORE+progress) = 0.6,
 * stalled rotation (EXPLORE stagnant) = 0.3,
 * no rotation (ASK/STORE/stuck) = 0.1. */
#define CSOS_BOYER_EXPLORE_HIGH   0.6
#define CSOS_BOYER_EXPLORE_LOW    0.3
#define CSOS_BOYER_STUCK          0.1

/* Vitality: Trend detection threshold.
 * Derived: Minimum measurable ΔpH across thylakoid membrane.
 * Changes below 5% are noise; above 5% indicate real state shift. */
#define CSOS_VITALITY_TREND_THRESH  0.05

/* Vitality: Low threshold — organism stressed.
 * Derived: Compensation point in photosynthesis where CO2 fixation
 * barely exceeds photorespiration. Below 30% = declining. */
#define CSOS_VITALITY_LOW         0.3

/* Vitality: Recovery threshold — organism stabilizing.
 * Derived: Light saturation point where photosynthetic rate plateaus.
 * Above 50% with positive trend = recovering. */
#define CSOS_VITALITY_RECOVER     0.5

/* ═══ EQUATION CONTRIBUTIONS (per-equation vitality components) ═══
 * Each equation's normalized contribution to the living equation.
 * All values 0.0 → 1.0. Updated every absorb cycle.
 */
typedef struct {
    double gouterman;    /* Resonance ratio: resonated / total signals */
    double forster;      /* Coupling health: mean coupling strength to peers */
    double marcus;       /* Prediction accuracy: 1 - normalized_F */
    double mitchell;     /* Life force rate: speed normalized to [0,1] */
    double boyer;        /* Decision clarity: 1.0=EXECUTE, 0.5=EXPLORE, 0.0=stuck */
    double vitality;     /* Geometric mean of all five */
    double vitality_ema; /* Exponential moving average (smoothed trend) */
    double vitality_peak;/* Highest vitality ever reached */
    uint32_t alive_cycles; /* Cycles where vitality > 0 (the organism's age) */
} csos_equation_t;

/* ═══ PROTOCOLS (how a signal entered the system) ═══ */
typedef enum {
    PROTO_INTERNAL  = 0,  /* Cross-ring cascade (cockpit ← domain) */
    PROTO_STDIO     = 1,  /* Terminal / pipe */
    PROTO_HTTP      = 2,  /* REST API */
    PROTO_UNIX      = 3,  /* Unix domain socket */
    PROTO_TCP       = 4,  /* TCP socket */
    PROTO_RDMA      = 5,  /* Remote direct memory access */
    PROTO_WEBHOOK   = 6,  /* Inbound event */
    PROTO_CRON      = 7,  /* Scheduled trigger */
    PROTO_LLM       = 8,  /* LLM-initiated observation */
} csos_proto_t;

/* ═══ DECISIONS (Boyer gate output) ═══ */
typedef enum {
    DECISION_EXPLORE = 0,  /* Need more signal */
    DECISION_EXECUTE = 1,  /* Enough evidence — deliver */
    DECISION_ASK     = 2,  /* Stuck, human present — ask ONE question */
    DECISION_STORE   = 3,  /* Stuck, autonomous — store question for later */
} csos_decision_t;

/* ═══ AGENT MODE (cross-living state) ═══ */
typedef enum {
    MODE_PLAN  = 0,  /* Observe only */
    MODE_BUILD = 1,  /* Can deliver */
} csos_mode_t;

/* ═══ UNIFIED PHOTON ═══
 * One struct carrying ALL context through the entire process.
 * Physics result + motor context + decision + Calvin eligibility.
 * This IS the IR between LLM and native process.
 */
typedef struct {
    /* Gouterman: signal identity */
    uint32_t  cycle;
    double    predicted;
    double    actual;
    double    error;
    int       resonated;

    /* Motor context: where it came from */
    uint32_t  substrate_hash;
    uint8_t   protocol;
    uint64_t  interval;         /* Cycles since last from this substrate */
    double    motor_strength;   /* Spaced repetition score */

    /* Mitchell: what it contributed */
    int32_t   delta;            /* Gradient change */

    /* Boyer: what it decided */
    uint8_t   decision;         /* csos_decision_t */

    /* Calvin: what it can teach */
    int       calvin_candidate; /* 1 if non-resonated → CO2 pool */

    /* Vitality: the living equation's pulse at this moment */
    double    vitality;         /* Unified living equation metric [0,1] */
} csos_photon_t;

/* ═══ ATOM (pigment in the membrane) ═══ */
typedef struct {
    char      name[CSOS_NAME_LEN];
    char      formula[CSOS_FORMULA_LEN];
    char      compute[CSOS_FORMULA_LEN];  /* Evaluable expression (safe-eval compatible) */
    char      source[CSOS_NAME_LEN];
    char      born_in[CSOS_NAME_LEN];
    double    params[CSOS_MAX_PARAMS];
    char      param_keys[CSOS_MAX_PARAMS][32];
    int       param_count;
    int       limit_count;
    double    spectral[2];      /* Absorption range [lo, hi] */
    int       broadband;        /* 1 = absorbs across full spectrum */
    double    rw;               /* Precomputed resonance_width */

    /* Photon history — fixed ring buffer, zero allocations on hot path */
    csos_photon_t *photons;       /* Ring buffer [CSOS_ATOM_PHOTON_RING] */
    int       photon_count;       /* Total photons ever seen (not ring size) */
    int       photon_head;        /* Next write position in ring */
    int       photon_cap;         /* Ring capacity (CSOS_ATOM_PHOTON_RING) */
    csos_photon_t *local_photons; /* Local ring buffer */
    int       local_count;
    int       local_head;
    int       local_cap;

    /* Prediction state */
    int       has_pending;
    double    pending_value;

    /* Cached last-resonated value — avoids O(n) backward scan per absorb */
    double    last_resonated_value;
    int       has_resonated;
} csos_atom_t;

/* ═══ MOTOR ENTRY (muscle memory for one substrate) ═══ */
typedef struct {
    uint32_t  substrate_hash;
    uint64_t  last_seen;
    uint64_t  interval;
    uint64_t  prev_interval;
    uint32_t  reps;
    double    strength;         /* 0.0 → 1.0, spaced rep scored */
} csos_motor_t;

/* ═══ MEMBRANE (the thylakoid — everything in one place) ═══ */
typedef struct {
    char      name[CSOS_NAME_LEN];

    /* Atoms (pigments) */
    csos_atom_t atoms[CSOS_MAX_ATOMS];
    int       atom_count;

    /* Running physics state (updated every absorb, never recomputed from scratch) */
    double    gradient;         /* Count of resonated photons */
    double    speed;            /* gradient / total_photons */
    double    F;                /* Rolling mean error */
    double    rw;               /* Resonance width of first atom */
    double    action_ratio;     /* resonated / total */
    uint32_t  cycles;
    uint32_t  signals;
    double    f_history[CSOS_FHIST_LEN];
    int       f_count;

    /* Motor memory (embedded, not separate transport layer) */
    csos_motor_t motor[CSOS_MAX_MOTOR];
    int       motor_count;
    uint64_t  motor_cycle;

    /* Agent state (embedded, not separate agent module) */
    uint8_t   mode;             /* csos_mode_t */
    uint8_t   decision;         /* csos_decision_t */
    int       consecutive_zero_delta;
    int       human_present;

    /* Mitchell proton count (n): loaded from spec ring definition.
     * Controls gradient accumulation rate: ΔG = -n·F·Δψ + 2.3RT·ΔpH.
     * Higher n = more gradient per resonated signal = faster to Boyer gate.
     * Default 1. Spec overrides via mitchell_n per ring. */
    int       mitchell_n;

    /* The Living Equation — unified vitality from all 5 equations.
     * Updated every absorb cycle. THE bridge between worlds. */
    csos_equation_t equation;

    /* Calvin CO2 pool (non-resonated actuals waiting for synthesis) */
    double    co2[CSOS_CO2_POOL_SIZE];
    int       co2_count;

    /* Photon ring (recent history for queries) */
    csos_photon_t ring[CSOS_PHOTON_RING];
    uint32_t  ring_head;

    /* Gradient gap: difference from upstream ring (Forster coupling strength signal).
     * Computed in organism_absorb: domain→cockpit→organism cascade.
     * Positive gap = upstream has more gradient = stronger Forster coupling.
     * Used to modulate cross-ring energy transfer rate. */
    double    gradient_gap;

    /* RDMA: if this membrane is remotely accessible */
    int       rdma_enabled;
    void     *rdma_mr;          /* ibv_mr* — memory region registration */
    uint32_t  rdma_rkey;        /* Remote access key */
    uint64_t  rdma_remote_addr; /* Peer buffer address */

    /* Coupling tensor: Forster strength to other membranes */
    struct {
        char     peer_name[CSOS_NAME_LEN];
        double   coupling;      /* (R0/r)^6 — computed from gradient ratio */
        uint32_t peer_node;     /* 0 = local, else remote node ID */
    } couplings[CSOS_MAX_RINGS];
    int       coupling_count;
} csos_membrane_t;

/* ═══ SEED (Calvin atom harvested from a completed session) ═══
 * When a session reaches Boyer EXECUTE and delivers, its Calvin atoms
 * are harvested into the seed bank. Future sessions are seeded with
 * matching atoms — Forster cross-pollination across time.
 *
 * Derived: Calvin cycle in photosynthesis produces G3P (seeds) that
 * feed the next growth cycle. The seed IS the living equation's legacy.
 */
#define CSOS_MAX_SEEDS       64
typedef struct {
    char      name[CSOS_NAME_LEN];
    char      formula[CSOS_FORMULA_LEN];
    char      compute[CSOS_FORMULA_LEN];
    char      source[CSOS_NAME_LEN];       /* Session that produced this seed */
    double    center;                       /* Pattern center value */
    double    strength;                     /* Motor strength at harvest time */
    uint32_t  substrate_hash;              /* Which substrate this seed resonates with */
    uint32_t  harvest_cycle;               /* When it was harvested */
} csos_seed_t;

/* ═══ SESSION = LIVING EQUATION ═══
 *
 * A session is NOT metadata about a conversation. It IS the living equation.
 * It has a body (membrane), senses (ingress), actions (egress), rhythm (schedule),
 * and connections to the outside world (substrate binding).
 *
 * The natural principle: a chloroplast doesn't wait for instructions.
 * Stomata open/close based on CO2 gradients. ATP synthase rotates when
 * the gradient is sufficient. The session runs itself.
 *
 *   substrate binding    → what it operates on (databricks, linkedin, aws)
 *   ring state           → its own gradient, speed, rw, Calvin atoms
 *   observation history  → what it's seen, what commands it ran
 *   human context        → preferences that carry across ALL sessions
 *   ingress/egress       → how it connects to the outside world
 *   connections          → which other equations it's converging with
 *   lifecycle            → created, last active, autonomous since, next
 *   scheduled            → when to run next (interval-driven, like circadian rhythm)
 *
 * The 5 equations drive everything:
 *   Gouterman: what signals does this session absorb? (spectral identity)
 *   Marcus:    how does it correct errors? (adapts with maturity)
 *   Mitchell:  how much evidence has it accumulated? (gradient = life force)
 *   Boyer:     is it ripe? (speed > rw → harvest/deliver)
 *   Calvin:    what patterns has it synthesized? (seeds for future)
 *   Forster:   can it cross-pollinate with other sessions? (convergence)
 */

typedef enum {
    SESSION_SEED    = 0,  /* Seeded but not yet active */
    SESSION_SPROUT  = 1,  /* First signals absorbed, building gradient */
    SESSION_GROW    = 2,  /* Active growth, Calvin synthesizing */
    SESSION_BLOOM   = 3,  /* Boyer EXECUTE — ready to deliver */
    SESSION_HARVEST = 4,  /* Delivered, Calvin atoms → seed bank */
    SESSION_DORMANT = 5,  /* Paused, motor memory persists */
} csos_session_stage_t;

/* ═══ INGRESS (stomata — how the session breathes in) ═══
 * Each session can have ONE ingress source. The scheduler calls it.
 * Derived: stomata open based on CO2 gradient. Ingress fetches based on schedule.
 *
 * Types: "command" (shell), "url" (HTTP fetch), "pipe" (stdin from agent)
 */
#define CSOS_INGRESS_CMD_LEN   512
#define CSOS_EGRESS_CMD_LEN    512
#define CSOS_MAX_OBS           64    /* Rolling observation history */

typedef struct {
    char    type[16];                /* "command", "url", "pipe", "" (none) */
    char    source[CSOS_INGRESS_CMD_LEN]; /* Shell command or URL to fetch */
    char    auth[128];               /* Auth header or env var name */
    int     timeout_ms;              /* Fetch timeout (default 5000) */
    int     active;                  /* 1 = enabled, 0 = paused */
} csos_ingress_t;

/* ═══ EGRESS (phloem — how the session delivers results) ═══
 * When Boyer says EXECUTE, egress fires automatically.
 * Derived: phloem transports sugars from leaves to the rest of the plant.
 *
 * Types: "webhook" (POST), "file" (write), "command" (pipe to shell)
 */
typedef struct {
    char    type[16];                /* "webhook", "file", "command", "" (none) */
    char    target[CSOS_EGRESS_CMD_LEN]; /* URL, file path, or shell command */
    char    format[32];              /* "json", "text", "csv" */
    int     active;                  /* 1 = enabled, 0 = paused */
} csos_egress_t;

/* ═══ SCHEDULE (circadian rhythm — when the session runs) ═══
 * Derived: circadian rhythm in plants. Stomata open at dawn, close at night.
 * The session has its own rhythm, independent of human interaction.
 */
typedef struct {
    int     interval_secs;           /* Seconds between ticks (0 = manual only) */
    int64_t last_tick;               /* Unix timestamp of last tick */
    int64_t next_tick;               /* Unix timestamp of next scheduled tick */
    int     tick_count;              /* Total autonomous ticks executed */
    int     autonomous;              /* 1 = runs without human, 0 = human-triggered */
} csos_schedule_t;

/* ═══ OBSERVATION (what the session has seen — short-term memory) ═══ */
typedef struct {
    int64_t timestamp;               /* Unix time */
    double  value;                   /* Absorbed value */
    int32_t delta;                   /* Gradient change */
    uint8_t decision;                /* Boyer output */
    double  vitality;                /* Vitality at this moment */
    char    summary[128];            /* Human-readable one-liner */
} csos_observation_t;

/* ═══ SESSION FLOW ELEMENT (one unit of operational context) ═══
 *
 * Every action within an agentic environment is a flow element.
 * Workflows, connections, absorbs, decisions — all are elements that
 * the session synthesizes into its living equation.
 *
 * Biology: each element is a metabolite in the chloroplast stroma.
 * The session's Calvin cycle synthesizes them into coherent patterns.
 */
#define CSOS_MAX_FLOW_ELEMENTS  64
#define CSOS_MAX_WORKFLOWS       8
#define CSOS_MAX_CONNECTIONS     8

typedef enum {
    FLOW_ABSORB     = 0,   /* Signal absorbed through membrane */
    FLOW_WORKFLOW   = 1,   /* Workflow step executed */
    FLOW_INGRESS    = 2,   /* External data fetched */
    FLOW_EGRESS     = 3,   /* Result delivered */
    FLOW_CONNECTION = 4,   /* Binding/connection established */
    FLOW_SCHEDULE   = 5,   /* Autonomous tick fired */
    FLOW_DECISION   = 6,   /* Boyer gate decision made */
    FLOW_SYNTHESIS  = 7    /* Calvin pattern synthesized */
} csos_flow_type_t;

typedef struct {
    csos_flow_type_t type;
    int64_t          timestamp;
    uint32_t         cycle;
    double           value;          /* Signal value or metric */
    int32_t          delta;          /* Gradient contribution */
    double           vitality;       /* Vitality at this moment */
    uint32_t         substrate_hash; /* Source substrate */
    char             label[64];      /* Human-readable: "workflow:etl step:extract" */
} csos_flow_element_t;

/* ═══ WORKFLOW TRACKER (per-session workflow participation) ═══
 *
 * A session operating in an agentic environment may run multiple workflows.
 * Each workflow is tracked: name, current step, total steps, health.
 *
 * Biology: multiple metabolic pathways running simultaneously in one cell.
 * The session tracks which pathways are active and their throughput.
 */
typedef struct {
    char    name[CSOS_NAME_LEN];     /* Workflow name */
    int     steps_total;             /* Total nodes in workflow */
    int     steps_completed;         /* Nodes executed */
    int     steps_failed;            /* Nodes that errored */
    double  throughput;              /* Successful steps / total attempts */
    int64_t started;                 /* Unix timestamp */
    int64_t last_step;              /* Last step timestamp */
    int     active;                  /* 1 = running, 0 = completed/paused */
} csos_workflow_track_t;

/* ═══ CONNECTION TRACKER (per-session external bindings) ═══
 *
 * A session may connect to multiple external systems within one conversation.
 * Each connection has liveness (last successful contact) and health.
 *
 * Biology: multiple transport proteins in one membrane section.
 * Each has its own opening frequency and substrate specificity.
 */
typedef struct {
    char    target[128];             /* "databricks:prod", "api:openmeteo", etc. */
    char    protocol[16];            /* "command", "url", "pipe", "webhook" */
    int64_t established;             /* When connection was created */
    int64_t last_contact;            /* Last successful data exchange */
    int     successes;               /* Successful exchanges */
    int     failures;                /* Failed exchanges */
    double  health;                  /* successes / (successes + failures) [0,1] */
    int     active;                  /* 1 = live, 0 = closed */
} csos_connection_t;

/* ═══ SESSION SYNTHESIS (the session-level living equation) ═══
 *
 * The session synthesizes ALL operational elements into one coherent metric.
 * This IS the living equation at the conversation level:
 *
 *   session_vitality = ⁵√(flow_health × workflow_health × connection_health
 *                         × ingress_health × egress_health)
 *
 * Each component [0,1]:
 *   flow_health       — fraction of flow elements with positive delta
 *   workflow_health    — geometric mean of active workflow throughputs
 *   connection_health  — geometric mean of active connection healths
 *   ingress_health     — ingress success rate
 *   egress_health      — egress delivery rate
 *
 * Biology: the cell's overall metabolic fitness — all pathways must function.
 * If ANY pathway collapses, the geometric mean pulls vitality toward zero.
 */
typedef struct {
    double  flow_health;             /* Positive-delta fraction of flow elements */
    double  workflow_health;         /* Geomean of workflow throughputs */
    double  connection_health;       /* Geomean of connection healths */
    double  ingress_health;          /* Ingress success rate */
    double  egress_health;           /* Egress delivery rate */
    double  session_vitality;        /* ⁵√(product of above) */
    double  session_vitality_ema;    /* Exponential moving average */
    double  session_vitality_peak;   /* Highest ever */
    int     total_elements;          /* Total flow elements recorded */
    int     total_positive;          /* Elements with delta > 0 */
    int     ingress_attempts;        /* Total ingress fetches */
    int     ingress_successes;       /* Successful fetches */
    int     egress_attempts;         /* Total egress deliveries attempted */
    int     egress_successes;        /* Successful deliveries */
} csos_session_synthesis_t;

#define CSOS_MAX_SESSIONS    32
typedef struct {
    char               id[CSOS_NAME_LEN];        /* Session identifier */
    char               substrate[CSOS_NAME_LEN];  /* Primary substrate this session maps to */
    uint32_t           substrate_hash;
    csos_session_stage_t stage;
    uint32_t           birth_cycle;               /* Organism cycle at session creation */
    uint32_t           last_active;               /* Organism cycle at last signal */
    double             peak_gradient;             /* Highest gradient reached */
    double             peak_speed;                /* Highest speed reached */
    int                deliveries;                /* Number of Boyer EXECUTE events */
    int                seeds_harvested;           /* Calvin atoms sent to seed bank */
    int                seeds_planted;             /* Seeds received from seed bank */

    /* ── THE LIVING EQUATION EXTENSIONS ── */

    /* Substrate binding: what external system this session operates on */
    char               binding[128];              /* e.g. "databricks:prod", "linkedin:jobs", "aws:s3" */

    /* Ingress: how signals enter (stomata) */
    csos_ingress_t     ingress;

    /* Egress: how results leave (phloem) */
    csos_egress_t      egress;

    /* Schedule: autonomous rhythm (circadian) */
    csos_schedule_t    schedule;

    /* Observation history: rolling window of what the session has seen */
    csos_observation_t observations[CSOS_MAX_OBS];
    int                obs_count;
    int                obs_head;                  /* Ring buffer head */

    /* Living equation vitality (cached from last tick) */
    double             vitality;
    double             vitality_trend;            /* Rising or falling */

    /* ── SESSION AS SYNTHESIZED LIVING EQUATION ── */

    /* Flow elements: every operational action within this session's scope */
    csos_flow_element_t flow[CSOS_MAX_FLOW_ELEMENTS];
    int                flow_count;
    int                flow_head;                 /* Ring buffer head */

    /* Workflow participation: all workflows active within this session */
    csos_workflow_track_t workflows[CSOS_MAX_WORKFLOWS];
    int                workflow_count;

    /* Connection registry: all external bindings in this session */
    csos_connection_t  connections[CSOS_MAX_CONNECTIONS];
    int                connection_count;

    /* Synthesis: the session-level living equation computed from all elements */
    csos_session_synthesis_t synthesis;
} csos_session_t;

/* ═══ SHARED EVENT LOG (the gradient between agent and canvas) ═══
 *
 * In photosynthesis, the proton gradient IS the communication between
 * photosystem II and ATP synthase. No messages. No protocols. Shared state.
 *
 * The event log is the proton gradient between Canvas and OpenCode agents.
 * Both systems write to it. Both read from it via SSE.
 * Bottlenecks surface automatically — no polling, no separate channel.
 */
#define CSOS_MAX_EVENTS      64
#define CSOS_EVENT_MSG_LEN   256

typedef enum {
    EVT_AGENT_ACTION  = 0,  /* Agent did something (absorb, bind, schedule) */
    EVT_CANVAS_ACTION = 1,  /* Canvas user did something (click, command) */
    EVT_BOTTLENECK    = 2,  /* Something is stuck (low vitality, ingress fail, zero delta) */
    EVT_RESOLUTION    = 3,  /* Bottleneck was resolved (vitality rising, ingress restored) */
    EVT_SCHEDULER     = 4,  /* Scheduler ticked sessions autonomously */
    EVT_MILESTONE     = 5,  /* Session reached new stage (bloom, harvest) */
} csos_event_type_t;

typedef struct {
    csos_event_type_t type;
    int64_t           timestamp;
    char              source[32];    /* "agent", "canvas", "scheduler", "session:<id>" */
    char              session[CSOS_NAME_LEN]; /* Which session, if any */
    char              message[CSOS_EVENT_MSG_LEN];
    double            vitality;      /* Organism vitality at event time */
    int               severity;      /* 0=info, 1=warning, 2=critical */
} csos_event_t;

/* ═══ AGENT MESSAGE CHANNEL ═══
 * The gradient between Canvas user and OpenCode agent.
 * Both write messages. Both read via SSE or polling.
 * The binary is the membrane — it doesn't interpret messages,
 * it just transports them like protons through the thylakoid.
 */
#define CSOS_MAX_MESSAGES    32
#define CSOS_MSG_LEN         1024

typedef struct {
    int64_t  timestamp;
    char     from[32];       /* "canvas", "agent", "scheduler" */
    char     to[32];         /* "agent", "canvas", "all" */
    char     body[CSOS_MSG_LEN];
    char     session[CSOS_NAME_LEN]; /* Context: which session, if any */
    int      read;           /* 1 = recipient has read it */
} csos_message_t;

/* ═══ ORGANISM (the greenhouse — container of membranes + sessions + seeds) ═══ */
typedef struct {
    csos_membrane_t *membranes[CSOS_MAX_RINGS];
    int              count;
    char             root[256];

    /* Seed bank: Calvin atoms harvested from completed sessions.
     * Forster cross-pollination across time — past sessions feed future ones. */
    csos_seed_t      seeds[CSOS_MAX_SEEDS];
    int              seed_count;

    /* Session registry: every conversation is a living equation */
    csos_session_t   sessions[CSOS_MAX_SESSIONS];
    int              session_count;

    /* Shared event log: the gradient between agent and canvas */
    csos_event_t     events[CSOS_MAX_EVENTS];
    int              event_count;
    int              event_head;     /* Ring buffer head */

    /* Agent message channel */
    csos_message_t   messages[CSOS_MAX_MESSAGES];
    int              msg_count;
    int              msg_head;       /* Ring buffer head */
} csos_organism_t;

/* ═══ THE CORE API ═══ */

/* Lifecycle */
int  csos_organism_init(csos_organism_t *org, const char *root);
void csos_organism_destroy(csos_organism_t *org);
csos_membrane_t *csos_organism_grow(csos_organism_t *org, const char *name);
csos_membrane_t *csos_organism_find(csos_organism_t *org, const char *name);

/* The ONE function — entire photosynthetic process */
csos_photon_t csos_membrane_absorb(csos_membrane_t *mem, double value,
                                    uint32_t substrate_hash, uint8_t protocol);

/* 3-ring cascade (domain → cockpit → organism) */
csos_photon_t csos_organism_absorb(csos_organism_t *org, const char *substrate,
                                    const char *raw, uint8_t protocol);

/* Forster coupling */
int  csos_membrane_diffuse(csos_membrane_t *src, csos_membrane_t *tgt);

/* The Living Equation — unified vitality query */
int  csos_membrane_equate(const csos_membrane_t *mem, char *json, size_t sz);
int  csos_organism_equate(const csos_organism_t *org, char *json, size_t sz);

/* Observation */
int  csos_membrane_see(const csos_membrane_t *mem, const char *detail,
                       char *json, size_t sz);

/* Health */
int  csos_membrane_lint(const csos_membrane_t *mem, char *json, size_t sz);

/* Motor memory query */
int  csos_motor_top(const csos_membrane_t *mem, uint32_t *hashes,
                    double *strengths, int max);
double csos_motor_strength(const csos_membrane_t *mem, uint32_t hash);

/* Atom operations */
void csos_atom_init(csos_atom_t *a, const char *name, const char *formula,
                    const char *source, const char **param_keys, int param_count);
void csos_atom_compute_rw(csos_atom_t *a);

/* ═══ GREENHOUSE API (session lifecycle + seed bank + convergence) ═══ */

/* Session lifecycle */
csos_session_t *csos_session_spawn(csos_organism_t *org, const char *id,
                                    const char *substrate);
csos_session_t *csos_session_find(csos_organism_t *org, const char *id);
void csos_session_update(csos_organism_t *org, csos_session_t *s,
                         const csos_photon_t *ph);

/* Shared event log (the gradient between agent and canvas) */
void csos_event_log(csos_organism_t *org, csos_event_type_t type,
                    const char *source, const char *session,
                    const char *message, int severity);
int  csos_event_list(const csos_organism_t *org, char *json, size_t sz);

/* Session = Living Equation: autonomous operation */
int  csos_session_bind(csos_session_t *s, const char *binding,
                       const char *ingress_type, const char *ingress_source,
                       const char *egress_type, const char *egress_target);
int  csos_session_schedule(csos_session_t *s, int interval_secs, int autonomous);
int  csos_session_tick(csos_organism_t *org, csos_session_t *s,
                       char *result_json, size_t sz);  /* ONE autonomous cycle */
int  csos_session_tick_all(csos_organism_t *org);      /* Tick all due sessions */
int  csos_session_observe(const csos_session_t *s, char *json, size_t sz);
int  csos_session_see_all(const csos_organism_t *org, char *json, size_t sz);

/* Session synthesis: infer and unify all operational elements */
void csos_session_record_flow(csos_session_t *s, csos_flow_type_t type,
                              uint32_t cycle, double value, int32_t delta,
                              double vitality, uint32_t substrate_hash,
                              const char *label);
int  csos_session_track_workflow(csos_session_t *s, const char *name,
                                 int steps_total);
int  csos_session_workflow_step(csos_session_t *s, const char *name,
                                int success);   /* 1=ok, 0=failed */
int  csos_session_track_connection(csos_session_t *s, const char *target,
                                    const char *protocol);
int  csos_session_connection_contact(csos_session_t *s, const char *target,
                                      int success);  /* 1=ok, 0=failed */
void csos_session_synthesize(csos_session_t *s);  /* Recompute session vitality */
int  csos_session_synthesize_json(const csos_session_t *s, char *json, size_t sz);

/* Seed bank: harvest Calvin atoms from a session, plant into new sessions */
int  csos_seed_harvest(csos_organism_t *org, const csos_session_t *s);
int  csos_seed_plant(csos_organism_t *org, csos_session_t *s);

/* Convergence: detect when two sessions' Calvin patterns overlap.
 * Returns Forster coupling strength (0 = no overlap, >1 = strong convergence).
 * Derived: Forster R₀/r where r = spectral distance between sessions. */
double csos_session_convergence(const csos_organism_t *org,
                                const csos_session_t *a,
                                const csos_session_t *b);

/* Merge: Forster-transfer Calvin atoms from src session into dst.
 * Only merges when convergence > threshold. Returns atoms transferred. */
int  csos_session_merge(csos_organism_t *org,
                        const csos_session_t *src, csos_session_t *dst);

/* Greenhouse persistence */
int  csos_seed_save(const csos_organism_t *org, const char *dir);
int  csos_seed_load(csos_organism_t *org, const char *dir);
int  csos_session_save(const csos_organism_t *org, const char *dir);
int  csos_session_load(csos_organism_t *org, const char *dir);

/* Greenhouse observation */
int  csos_greenhouse_see(const csos_organism_t *org, char *json, size_t sz);

/* RDMA coupling tensor */
int  csos_membrane_couple(csos_membrane_t *a, csos_membrane_t *b);
int  csos_membrane_rdma_register(csos_membrane_t *m);  /* Register for remote access */
int  csos_membrane_rdma_diffuse(csos_membrane_t *src, const char *remote_name,
                                 uint32_t remote_node); /* Cross-node Forster transfer */
double csos_membrane_coupling_strength(const csos_membrane_t *m, const char *peer);

/* ═══ SPEC PARSER (reads .csos files — no hardcoded equations) ═══ */

/* Parsed atom definition from .csos spec */
typedef struct {
    char   name[CSOS_NAME_LEN];
    char   formula[CSOS_FORMULA_LEN];
    char   compute[CSOS_FORMULA_LEN];
    char   source[CSOS_NAME_LEN];
    char   param_keys[CSOS_MAX_PARAMS][32];
    double param_defaults[CSOS_MAX_PARAMS];
    int    param_count;
    double spectral[2];
    int    broadband;
} csos_spec_atom_t;

/* Benchmark substrate profile — spec-driven routing.
 * Each benchmark maps to a spectral range and Calvin/RW hints.
 * Derived: Gouterman spectral identity per substrate type. */
#define CSOS_MAX_SUBSTRATES  16
typedef struct {
    char      name[CSOS_NAME_LEN];       /* e.g. "mmlu", "gsm8k" */
    double    spectral[2];               /* Gouterman: absorption range */
    double    rw_hint;                   /* Suggested resonance width */
    int       calvin_freq_hint;          /* Suggested Calvin frequency */
} csos_substrate_profile_t;

/* Parsed spec result */
typedef struct {
    csos_spec_atom_t atoms[CSOS_MAX_ATOMS];
    int              atom_count;
    char             ring_names[CSOS_MAX_RINGS][CSOS_NAME_LEN];
    int              ring_mitchell_n[CSOS_MAX_RINGS]; /* Mitchell n per ring (from spec) */
    int              ring_count;
    csos_substrate_profile_t substrates[CSOS_MAX_SUBSTRATES];
    int              substrate_count;
} csos_spec_t;

/* Parse a .csos spec file. Returns 0 on success. */
int  csos_spec_parse(const char *path, csos_spec_t *spec);

/* Find substrate profile by name. Returns NULL if not found. */
const csos_substrate_profile_t *csos_spec_find_substrate(
    const csos_spec_t *spec, const char *name);

/* Load Calvin atoms from .mem.json files into spec. Returns atoms added. */
int  csos_spec_load_calvin(const char *rings_dir, csos_spec_t *spec);

/* Create membrane from parsed spec (replaces hardcoded EQUATIONS[]) */
csos_membrane_t *csos_membrane_from_spec(const csos_spec_t *spec, int ring_index);

/* ═══ FORMULA EVALUATION (runtime, non-JIT fallback) ═══ */

/* Evaluate a compute expression with given params and signal value.
 * Generic: works for ANY equation. No name-based dispatch. */
double csos_formula_eval(const char *compute, const double *params,
                         const char param_keys[][32], int param_count,
                         double signal);

#ifdef CSOS_HAS_LLVM
/* ═══ FORMULA JIT (compile compute expressions to LLVM IR) ═══ */

/* Compile all atom compute expressions for a membrane into native code.
 * Returns 0 on success. After this, csos_formula_jit_eval() uses native code. */
int  csos_formula_jit_compile(csos_membrane_t *m);

/* Evaluate atom i's compute expression using JIT'd code.
 * Falls back to csos_formula_eval() if JIT not available. */
double csos_formula_jit_eval(int atom_index, const double *params,
                             int param_count, double signal);

/* Check if formula JIT needs recompile (after Calvin synthesis) */
int  csos_formula_jit_check(csos_membrane_t *m);
#endif

#endif /* CSOS_MEMBRANE_H */
