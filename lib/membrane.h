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
    char      source[CSOS_NAME_LEN];
    char      born_in[CSOS_NAME_LEN];
    double    params[CSOS_MAX_PARAMS];
    char      param_keys[CSOS_MAX_PARAMS][32];
    int       param_count;
    int       limit_count;
    double    rw;               /* Precomputed resonance_width */

    /* Photon history (evidence this atom has accumulated) */
    csos_photon_t *photons;
    int       photon_count;
    int       photon_cap;
    csos_photon_t *local_photons;
    int       local_count;
    int       local_cap;

    /* Prediction state */
    int       has_pending;
    double    pending_value;
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

#endif /* CSOS_MEMBRANE_H */
