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

/* Forster: Motor strength decay per cycle.
 * Derived: D1 protein half-life decay ≈ 0.99 per cycle (1% loss). */
#define CSOS_MOTOR_DECAY          0.99

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

/* Calvin: Synthesis frequency (every N cycles).
 * Derived: Rubisco catalytic rate ≈ 3 reactions/sec × ~2s observation = 5-6.
 * Calvin cycle runs every 5 membrane cycles. */
#define CSOS_CALVIN_FREQUENCY     5

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

    /* Calvin CO2 pool (non-resonated actuals waiting for synthesis) */
    double    co2[CSOS_CO2_POOL_SIZE];
    int       co2_count;

    /* Photon ring (recent history for queries) */
    csos_photon_t ring[CSOS_PHOTON_RING];
    uint32_t  ring_head;

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

/* ═══ ORGANISM (container of membranes — replaces Core) ═══ */
typedef struct {
    csos_membrane_t *membranes[CSOS_MAX_RINGS];
    int              count;
    char             root[256];
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

/* Parsed spec result */
typedef struct {
    csos_spec_atom_t atoms[CSOS_MAX_ATOMS];
    int              atom_count;
    char             ring_names[CSOS_MAX_RINGS][CSOS_NAME_LEN];
    int              ring_mitchell_n[CSOS_MAX_RINGS]; /* Mitchell n per ring (from spec) */
    int              ring_count;
} csos_spec_t;

/* Parse a .csos spec file. Returns 0 on success. */
int  csos_spec_parse(const char *path, csos_spec_t *spec);

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
