/*
 * CSOS Membrane — F->0 Architecture
 *
 * F = COMPLEXITY - ACCURACY. dF/dt <= 0 at all times.
 *
 * Three layers, one membrane:
 *   EXTERNAL:  Causal atoms predict effects before they manifest
 *   INTERNAL:  Hierarchical compression (L0-L4, 5000:1 total)
 *   BRIDGE:    Active inference targets highest F gap
 *
 * THREE types mirror the chemistry:
 *   Photon   — A signal event carrying ALL context
 *   Atom     — A pigment that predicts, observes, resonates, and tunes
 *   Membrane — The thylakoid: atoms + motor trace + gradient + decision state
 *
 * ONE function processes everything: membrane_absorb()
 *   Photon enters -> Gouterman -> Causal Inject -> Marcus -> Mitchell
 *   -> Forster -> Boyer -> Calvin -> Hierarchy -> Rhythm -> Prune
 *   All in one pass. No layers. No message passing.
 */
#ifndef CSOS_MEMBRANE_H
#define CSOS_MEMBRANE_H

#include <stdint.h>
#include <stddef.h>

/* ═══ PHYSICAL CONSTANTS (the 4 absolute limits) ═══
 *
 * These are the ONLY hardcoded values in the system. Each traces to an
 * irreducible physical law that cannot be computed away:
 *
 *   1. Landauer's limit:      kT·ln(2) ≈ 2.85×10⁻²¹ J per bit erasure
 *   2. Nyquist theorem:       Must sample at ≥ 2× highest signal frequency
 *   3. Kolmogorov complexity:  Irreducible description length (can't compress further)
 *   4. Gödel incompleteness:   Self-referential system can't fully predict itself
 *
 * Everything else — thresholds, learning rates, decay, decision gates —
 * derives from the 5 equations and runtime observables.
 */

/* Foundation atoms: one per equation. This IS the Kolmogorov lower bound —
 * fewer than 5 atoms cannot represent the 5 independent degrees of freedom
 * (resonance, coupling, error, gradient, decision). */
#define CSOS_FOUNDATION_ATOMS  5

/* ═══ STRUCTURAL LIMITS (derived from physical constants) ═══
 *
 * Each limit traces to a physical law. The derivation is documented inline.
 * All ring buffers are powers of 2 for O(1) modular arithmetic (& mask).
 *
 * OPERATING PRINCIPLE: If you cannot name the physical law that sets
 * the limit, it should be a runtime observable, not a #define.
 */

/* PHOTON_RING: Nyquist theorem.
 * To detect rhythms of period P, need ≥ 2P samples (Nyquist).
 * Longest meaningful market rhythm ≈ quarterly (≈ 4096 at sub-second tick).
 * PHOTON_RING = 2^ceil(log₂(2 × max_detectable_period)).
 * This also sets F_floor = 1/PHOTON_RING = the observation resolution limit.
 * Increasing this improves F_floor but costs sizeof(photon) × N memory. */
#define CSOS_PHOTON_RING       8192   /* 2^13: Nyquist for period ≤ 4096 */

/* ATOM_PHOTON_RING: Nyquist per-atom.
 * Each atom sees a subset of signals. Per-atom Nyquist depth =
 * PHOTON_RING / MAX_ATOMS × 2 (oversampling for sparse substrates).
 * 8192/32 × 2 = 512. Power of 2 for mask. */
#define CSOS_ATOM_PHOTON_RING  512    /* 2^9: per-atom Nyquist depth */

/* MAX_ATOMS: Kolmogorov complexity bound.
 * F = COMPLEXITY - ACCURACY. At equilibrium (F→0):
 *   COMPLEXITY = Σ(hierarchy_cost_i) / MAX_ATOMS
 *   ACCURACY ≈ 1 - F_floor
 * Each atom adds 1/MAX_ATOMS to complexity at L0.
 * With 5-level hierarchy (compression [1, 0.1, 0.02, 0.002, 0.02]):
 *   Effective capacity = MAX_ATOMS × mean_compression ≈ MAX_ATOMS × 100
 * MAX_ATOMS = FOUNDATION_ATOMS + ceil(PHOTON_RING^(1/3))
 *           = 5 + ceil(8192^0.333) = 5 + 21 ≈ 26 → round to 2^5 = 32.
 * This ensures compressed atoms can span the full observation depth. */
#define CSOS_MAX_ATOMS         32     /* 2^5: Kolmogorov F-equilibrium */

/* MAX_PARAMS: Gödel incompleteness bound.
 * Each atom's formula has finite descriptive power.
 * params = coefficients of the atom's prediction model.
 * With formula DOF from Gouterman (dof = params + limits + formula_complexity),
 * rw = dof/(dof+1) → at 16 params, rw = 0.94 (near-unity).
 * Beyond 16, marginal rw gain < 0.003 — information-theoretically negligible.
 * MAX_PARAMS = ceil(-log₂(1 - rw_target)) where rw_target = 0.94 → ceil(4.06) × 4 = 16. */
#define CSOS_MAX_PARAMS        16     /* Gödel: diminishing descriptive returns */

/* MAX_RINGS: Mitchell chemiosmotic compartments.
 * Mitchell's proton gradient requires distinct compartments (thylakoid membranes).
 * 3 rings are defined by the spec (domain, cockpit, organism).
 * Additional rings for cross-domain coupling: one per domain pair.
 * MAX_RINGS ≥ 3 + (max_domains × (max_domains-1))/2
 * For 5 domains: 3 + 10 = 13 → round to 2^4 = 16. */
#define CSOS_MAX_RINGS         16     /* 2^4: Mitchell compartment limit */

/* MAX_MOTOR: Substrate memory capacity.
 * Motor memory = spaced repetition over distinct substrates.
 * Each substrate needs one entry. With MAX_ATOMS atoms × MAX_RINGS rings,
 * maximum distinguishable substrates = MAX_ATOMS × MAX_RINGS = 512.
 * But most substrates share atoms. Practical: MAX_ATOMS × (MAX_RINGS/2).
 * 32 × 8 = 256. This is the working memory capacity — analogous to
 * the number of distinct patterns the organism can track simultaneously. */
#define CSOS_MAX_MOTOR         256    /* MAX_ATOMS × MAX_RINGS/2 */

/* CO2_POOL_SIZE: Calvin cycle substrate buffer.
 * Non-resonated signals accumulate as CO₂ (raw material for pattern synthesis).
 * Pool must hold enough signals for statistical significance:
 *   CO2_POOL = MAX_MOTOR × (minimum_reps_for_pattern)
 *   256 × 1 = 256. But pattern detection needs variance, so ≥ 2 reps:
 *   CO2_POOL ≈ MAX_MOTOR. Conveniently 2^8 = 256. */
#define CSOS_CO2_POOL_SIZE     256    /* 2^8: Calvin substrate capacity */

/* FHIST_LEN: Regime detection history.
 * Running F history for mean/σ computation (regime thresholds).
 * Must span several regime transitions for accurate σ estimation.
 * At ~100 cycles per regime transition and ~10 transitions for stable σ:
 *   FHIST = 100 × 10 = 1000 → round to 2^10 = 1024. */
#define CSOS_FHIST_LEN        1024   /* 2^10: regime σ estimation depth */

/* MAX_SEEDS: Greenhouse seed bank.
 * Calvin atoms harvested from completed sessions.
 * Each seed is a compressed pattern (L1 atom). Seed bank capacity =
 * MAX_ATOMS × 2 (each membrane can contribute up to MAX_ATOMS patterns,
 * but pruning ensures only net-positive atoms survive).
 * 32 × 2 = 64. */
#define CSOS_MAX_SEEDS         64     /* MAX_ATOMS × 2: seed bank */

/* String buffers: Kolmogorov description lengths.
 * NAME_LEN: Human-readable identifier. Mean English word = 5 chars.
 * Longest meaningful atom name ≈ 12 words = 60 chars → round to 64.
 * FORMULA_LEN: Mathematical expression. Longest spec formula (Marcus):
 *   "exp(-(dG + l) ** 2 / (4 * l * V)) * input" = 44 chars.
 *   Calvin formulas grow: "pattern@123.456+/-78.9" ≈ 25 chars.
 *   With safety margin: 128 = 2^7. */
#define CSOS_NAME_LEN          64     /* 2^6: identifier description length */
#define CSOS_FORMULA_LEN       128    /* 2^7: formula description length */

/* Calvin pattern detection: derived from CO2 pool and statistical minimums.
 * SAMPLE_SIZE: minimum samples for variance estimation.
 *   For t-distribution at 95% confidence with df=49: t ≈ 2.01.
 *   50 samples gives ≈ 14% margin of error on σ. This is the statistical
 *   minimum for reliable pattern detection (not a tunable constant).
 * MATCH_DEPTH: resonated photons to check for duplicate patterns.
 *   With atom rw ≈ 0.85-0.92, resonation rate ≈ 85-92%.
 *   10 checks at 85% resonation → P(miss) < 0.001. */
#define CSOS_CALVIN_SAMPLE_SIZE  50   /* Statistical: 95% CI on variance */
#define CSOS_CALVIN_MATCH_DEPTH  10   /* Statistical: P(miss) < 0.001 */

/* MAX_SUBSTRATES: Spec parser substrate array.
 * Number of substrate types definable in eco.csos.
 * Current spec has 13 substrates. Headroom for domain onboarding.
 * MAX_SUBSTRATES = MAX_RINGS (each ring can specialize on a substrate set). */
#define CSOS_MAX_SUBSTRATES    16     /* = MAX_RINGS: spec parser capacity */

/* ═══ OPERATING PRINCIPLES ═══
 *
 * These 5 principles guard Law I: "Only physical limits are hardcoded."
 * Every agent, mechanism, and threshold must satisfy ALL five.
 *
 * OP-1: PHYSICAL TRACEABILITY
 *   Every #define must name the physical law it derives from in its comment.
 *   If no law can be named → it must become a csos_derive_*() function.
 *
 * OP-2: EQUATION SOVEREIGNTY
 *   All runtime decisions flow from the 5 equations (Gouterman, Marcus,
 *   Mitchell, Förster, Boyer). No if/else on domain names, substrate types,
 *   or signal values. The equations decide; agents execute.
 *
 * OP-3: OBSERVABLE DERIVATION
 *   Every threshold is a function of membrane observables (gradient, speed,
 *   F, atom.rw, motor.strength, photon_count, spectral range). If the
 *   membrane can compute it, agents must not hardcode it.
 *
 * OP-4: CONVERGENCE GUARANTEE
 *   Structural limits must preserve dF/dt ≤ 0:
 *     - PHOTON_RING ≥ 2 × max_detectable_period (Nyquist)
 *     - MAX_ATOMS ≥ FOUNDATION_ATOMS + PHOTON_RING^(1/3) (Kolmogorov)
 *     - MAX_MOTOR ≥ MAX_ATOMS × MAX_RINGS/2 (substrate coverage)
 *     - CO2_POOL ≥ MAX_MOTOR (Calvin fuel)
 *   Violating any of these breaks the F→0 guarantee.
 *
 * OP-5: DIMENSIONAL CONSISTENCY
 *   Limits that interact must scale together:
 *     ATOM_PHOTON_RING = PHOTON_RING / MAX_ATOMS × 2
 *     MAX_MOTOR = MAX_ATOMS × MAX_RINGS / 2
 *     MAX_SEEDS = MAX_ATOMS × 2
 *     CO2_POOL = MAX_MOTOR
 *   Changing one limit requires rechecking its dependents.
 */

/* ═══ PHYSICS CONSTANTS — ALL DERIVED FROM THE 5 EQUATIONS ═══ */
/*
 * Law I compliance: NO magic numbers. Every threshold is a function of
 * observables already computed by the membrane. The 5 equations provide
 * the natural scales:
 *
 * Gouterman 1961:  dE = hc/lambda           → resonance width from atom DOF
 * Forster 1948:    k = (1/tau)(R0/r)^6       → coupling from transfer efficiency
 * Marcus 1956:     k = exp(-(dG+l)^2/4lkT)  → error guard from signal variance
 * Mitchell 1961:   dG = -nF*dpsi + 2.3RT*dpH → gradient equilibrium from n
 * Boyer 1997:      ATP = flux*n/3            → decision threshold from flux
 */

/* ═══ REGIME STATES ═══ */
#define CSOS_REGIME_BULL    0
#define CSOS_REGIME_BEAR    1
#define CSOS_REGIME_CRISIS  2

/* ═══ EQUATION CONTRIBUTIONS ═══ */
typedef struct {
    double gouterman;
    double forster;
    double marcus;
    double mitchell;
    double boyer;
    double vitality;
    double vitality_ema;
    double vitality_peak;
    uint32_t alive_cycles;
} csos_equation_t;

/* ═══ PROTOCOLS ═══ */
typedef enum {
    PROTO_INTERNAL  = 0,
    PROTO_STDIO     = 1,
    PROTO_HTTP      = 2,
    PROTO_UNIX      = 3,
    PROTO_TCP       = 4,
    PROTO_WEBHOOK   = 5,
    PROTO_CRON      = 6,
    PROTO_LLM       = 7,
} csos_proto_t;

/* ═══ DECISIONS (Boyer gate output) ═══ */
typedef enum {
    DECISION_EXPLORE = 0,
    DECISION_EXECUTE = 1,
    DECISION_ASK     = 2,
    DECISION_STORE   = 3,
} csos_decision_t;

/* ═══ AGENT MODE ═══ */
typedef enum {
    MODE_PLAN  = 0,
    MODE_BUILD = 1,
} csos_mode_t;

/* ═══ UNIFIED PHOTON ═══
 * One struct carrying ALL context through the entire process.
 * This IS the IR between LLM and native process.
 */
typedef struct {
    /* Gouterman: signal identity */
    uint32_t  cycle;
    double    predicted;
    double    actual;
    double    error;
    int       resonated;

    /* Motor context */
    uint32_t  substrate_hash;
    uint8_t   protocol;
    uint64_t  interval;
    double    motor_strength;

    /* Mitchell: what it contributed */
    int32_t   delta;

    /* Boyer: what it decided */
    uint8_t   decision;

    /* Calvin: what it can teach */
    int       calvin_candidate;

    /* Vitality: the living equation's pulse */
    double    vitality;

    /* F->0: Active inference recommendation for LLM */
    uint32_t  probe_target;
} csos_photon_t;

/* ═══ ATOM (pigment in the membrane) ═══ */
typedef struct {
    char      name[CSOS_NAME_LEN];
    char      formula[CSOS_FORMULA_LEN];
    char      compute[CSOS_FORMULA_LEN];
    char      source[CSOS_NAME_LEN];
    char      born_in[CSOS_NAME_LEN];
    double    params[CSOS_MAX_PARAMS];
    char      param_keys[CSOS_MAX_PARAMS][32];
    int       param_count;
    int       limit_count;
    double    spectral[2];      /* Absorption range [lo, hi] */
    int       broadband;
    double    rw;               /* Precomputed resonance_width */

    /* Photon history ring buffer */
    csos_photon_t *photons;
    int       photon_count;
    int       photon_head;
    int       photon_cap;
    csos_photon_t *local_photons;
    int       local_count;
    int       local_head;
    int       local_cap;

    /* Prediction state */
    int       has_pending;
    double    pending_value;

    /* Cached last-resonated value */
    double    last_resonated_value;
    int       has_resonated;

    /* ═══ F->0: Hierarchy Level ═══
     *   0 = raw signal atom (33:1 compression)
     *   1 = pattern sequence (10:1 — Calvin atoms)
     *   2 = regime state (50:1 — HMM-emergent)
     *   3 = causal mechanism (500:1 — directed cause->effect)
     *   4 = temporal rhythm (52:1 — Fourier periodic) */
    uint8_t   hierarchy_level;

    /* ═══ F->0: Causal Atom Fields (L3) ═══
     * CausalAtom {
     *   cause_hash: fnv1a("Fed_policy")
     *   effect_hash: fnv1a("VIX")
     *   lag: 30 minutes
     *   strength: 0.85
     *   direction: +1 (same) or -1 (inverse)
     *   counterfactual_score: 0.72
     * }
     */
    uint32_t  causal_target;        /* 0 = not causal, else target substrate hash */
    uint16_t  causal_lag;           /* Cycles between cause and effect */
    double    causal_strength;      /* Forster x Marcus of the link */
    int8_t    causal_direction;     /* +1 (same) or -1 (inverse) */
    double    counterfactual_score; /* [0,1]: 0=bad model, 0.5=bad luck, 1.0=regime change */

    /* ═══ F->0: Regime Atom Fields (L2 — HMM) ═══
     * RegimeAtoms encode Hidden Markov Model:
     *   BULL(P->BULL=0.995, P->TRANSITION=0.004, P->CRISIS=0.001)
     *   BEAR(P->BEAR=0.98, ...)
     * The regime biases all atom resonance. */
    double    transition_probs[3];  /* P(->BULL), P(->BEAR), P(->CRISIS) */
    uint8_t   current_regime;       /* 0=BULL, 1=BEAR, 2=CRISIS */

    /* ═══ F->0: Rhythm Atom Fields (L4 — Fourier) ═══
     * One rhythm atom captures ALL instances of a periodicity.
     * Monday rebalancing = period ~5 trading days. */
    uint16_t  rhythm_period;
    uint16_t  rhythm_phase;
    double    rhythm_amplitude;

    /* ═══ F->0: Per-atom F contribution ═══
     * net_value = accuracy_contribution - complexity_cost
     * Atoms with net_value < 0 are pruning candidates (L1 regularization). */
    double    accuracy_contribution;
    double    complexity_cost;
    double    net_value;

    /* ═══ F->0 ECONOPHYSICS: 7 Atom Type Extensions ═══
     * These fields encode what current econophysics misses.
     * All processed by the same 5 equations through membrane_absorb().
     * No parallel path. No separate engine. Physics drives everything. */

    /* Gap 1: SelfImpactAtom (reflexivity — observer IS the system)
     * Soros's reflexivity: your orders change prices.
     * reflexivity_ratio = observed/expected. 1.0 = no self-impact. */
    double    self_impact_expected;
    double    self_impact_observed;
    double    reflexivity_ratio;

    /* Gap 2: InfoDecayAtom (information half-life per signal type)
     * tick-level: 1 cycle. earnings: 100 cycles. macro: 5000 cycles.
     * info_remaining decays exponentially. Stale atoms lose accuracy. */
    double    info_half_life;
    double    info_remaining;

    /* Gap 3: SpectralAtom (frequency-dependent information weighting)
     * HF = mostly noise (weight 0.3x). LF = mostly signal (weight 3x).
     * Boyer gradient multiplier: low-freq evidence counts more per bit. */
    uint8_t   spectral_band;      /* 0=HF(noise), 1=MF(mixed), 2=LF(signal) */
    double    spectral_weight;    /* Boyer gradient multiplier */

    /* Gap 4: AdversarialAtom (edge decay from crowding)
     * High-edge + crowded = fast death. Natural selection for alpha.
     * edge_decay_rate = edge_strength * crowding_score. */
    double    edge_strength;
    double    crowding_score;
    double    edge_decay_rate;

    /* Gap 5: AgentTypeAtom (order flow agent classification)
     * 0=unknown, 1=HFT, 2=institutional, 3=retail, 4=central_bank.
     * Agent mix IS the regime. */
    uint8_t   agent_type;
    double    agent_signature;

    /* Gap 6: DirectedCouplingAtom (asymmetric A->B with lag)
     * Fed -> bonds (15min) -> equity (2hr). Predict last from first.
     * Total horizon = sum of intermediate lags. */
    uint32_t  upstream_hash;
    uint32_t  downstream_hash;
    uint16_t  flow_lag;
    double    flow_strength;

    /* Gap 7: OrderBookAtom (microstructure gradient)
     * bid_depth = potential energy on buy side.
     * book_imbalance = (bid-ask)/(bid+ask) — Mitchell gradient. */
    double    bid_depth;
    double    ask_depth;
    double    book_imbalance;

    /* V13: Do-calculus Level 2 — intervention tracking.
     * Counts how many times this causal atom resonated from our own
     * actions (PROTO_INTERNAL/LLM). High count = self-referential loop. */
    uint32_t  intervention_count;
} csos_atom_t;

/* ═══ MOTOR ENTRY (muscle memory for one substrate) ═══ */
typedef struct {
    uint32_t  substrate_hash;
    uint64_t  last_seen;
    uint64_t  interval;
    uint64_t  prev_interval;
    uint32_t  reps;
    double    strength;         /* 0.0 -> 1.0, spaced rep scored */
} csos_motor_t;

/* ═══ MEMBRANE (the thylakoid — everything in one place) ═══ */
typedef struct {
    char      name[CSOS_NAME_LEN];

    /* Atoms (pigments) */
    csos_atom_t atoms[CSOS_MAX_ATOMS];
    int       atom_count;

    /* Running physics state */
    double    gradient;
    double    speed;
    double    F;                /* Rolling mean error */
    double    rw;
    double    action_ratio;
    uint32_t  cycles;
    uint32_t  signals;
    double    f_history[CSOS_FHIST_LEN];
    int       f_count;

    /* Motor memory */
    csos_motor_t motor[CSOS_MAX_MOTOR];
    int       motor_count;
    uint64_t  motor_cycle;

    /* Agent state */
    uint8_t   mode;
    uint8_t   decision;
    int       consecutive_zero_delta;
    int       human_present;

    /* Mitchell proton count */
    int       mitchell_n;

    /* The Living Equation */
    csos_equation_t equation;

    /* Calvin CO2 pool */
    double    co2[CSOS_CO2_POOL_SIZE];
    int       co2_count;

    /* Photon ring */
    csos_photon_t ring[CSOS_PHOTON_RING];
    uint32_t  ring_head;

    /* ═══ F->0: Free Energy Decomposition ═══
     * F = COMPLEXITY - ACCURACY
     * dF/dt <= 0 at all times = system always learning */
    double    F_accuracy;         /* 1 / (1 + F) */
    double    F_complexity;       /* weighted atom count / MAX_ATOMS */
    double    F_free_energy;      /* F_complexity - F_accuracy */
    double    F_prev;             /* F from previous cycle */
    double    dF_dt;              /* F - F_prev (should be <= 0) */

    /* ═══ F->0: Structural Guarantee ═══ */
    uint32_t  dF_positive_streak; /* Consecutive dF/dt > 0 cycles */
    uint32_t  probe_target;       /* Active inference: substrate to probe next */
    uint8_t   active_regime;      /* 0=BULL, 1=BEAR, 2=CRISIS */

    /* Forster coupling gradient gap */
    double    gradient_gap;

    /* ═══ V14: DUAL-REACTOR COUPLING (Thylakoid Architecture) ═══
     *
     * Photosynthesis splits into two physically separated reactors:
     *
     *   LIGHT REACTOR (thylakoid lumen + membrane):
     *     Gouterman + Förster + Marcus = complexity reduction
     *     Absorbs raw photons, filters noise, transfers energy, gates errors.
     *     Produces ATP + NADPH (compressed energy carriers).
     *
     *   DARK REACTOR (stroma / Calvin cycle):
     *     Mitchell + Boyer = accuracy maximization
     *     Consumes ATP + NADPH to fix CO₂ into glucose (predictive models).
     *     Produces: L1 patterns, L3 causal atoms, L4 rhythms, decisions.
     *
     * COUPLING: ATP + NADPH shuttle between reactors.
     *   - Light produces at rate: resonation_count per cycle
     *   - Dark consumes at rate: calvin_attempts + hierarchy_rebuilds per cycle
     *   - Dark cannot run faster than light produces (backpressure)
     *   - Overflow triggers photoprotection (aggressive L0 pruning)
     *
     * This replaces the monolithic freeenergy_post_absorb() with two
     * independent reactor cycles coupled by an explicit energy budget.
     */

    /* ATP pool: energy currency between light and dark reactors.
     * Produced by: Gouterman resonation (1 ATP per resonated photon)
     * Consumed by: Calvin synthesis (3 ATP per pattern attempt),
     *              Causal discovery (5 ATP per link tested),
     *              Hierarchy rebuild (1 ATP per level assignment).
     * When ATP = 0, dark reactor pauses.
     * When ATP > ATP_OVERFLOW, light reactor triggers photoprotection. */
    double    atp_pool;
    double    atp_produced_this_cycle;
    double    atp_consumed_this_cycle;
    double    atp_overflow_threshold;  /* Derived: MAX_ATOMS × mitchell_n */

    /* NADPH pool: reducing power (prediction confidence).
     * Produced by: Marcus gating (1 NADPH per error < λ)
     * Consumed by: Counterfactual scoring (1 NADPH per validation),
     *              Regime HMM update (2 NADPH per transition check).
     * NADPH is the "confidence token" — you can only validate models
     * if the light reactor has confirmed signal quality. */
    double    nadph_pool;
    double    nadph_produced_this_cycle;
    double    nadph_consumed_this_cycle;

    /* Reactor cycle counters (for independent rate tuning) */
    uint32_t  light_cycles;       /* Total light reactor firings */
    uint32_t  dark_cycles;        /* Total dark reactor firings */
    double    light_rate;         /* Measured: light cycles per wall-second */
    double    dark_rate;          /* Measured: dark cycles per wall-second */

    /* Photoprotection state (non-photochemical quenching).
     * When signal rate overwhelms absorb capacity, excess energy
     * dissipates as heat instead of being processed.
     * npq_active = 1 when ATP > overflow threshold. */
    uint8_t   npq_active;         /* 0=normal, 1=photoprotecting */
    double    npq_dissipated;     /* Cumulative energy dissipated as heat */

    /* Reactor health: light/dark balance ratio.
     * Ideal: 1.0 (balanced). >1 = light overproducing. <1 = dark starving.
     * balance = atp_produced / (atp_consumed + ε)
     * Used by Boyer to scale decision confidence. */
    double    reactor_balance;

    /* Substrate-level reflexivity propagation (V14 fix).
     * V13 tracked reflexivity per-atom only. V14 propagates to all atoms
     * sharing a substrate when any atom on that substrate detects self-impact.
     * This prevents under-counting market impact across multiple atom views. */
    double    substrate_reflexivity[CSOS_MAX_MOTOR];

    /* Coupling tensor: Forster strength to other membranes */
    struct {
        char     peer_name[CSOS_NAME_LEN];
        double   coupling;
    } couplings[CSOS_MAX_RINGS];
    int       coupling_count;
} csos_membrane_t;

/* ═══ SEED (Calvin atom harvested from a completed session) ═══ */
#define CSOS_MAX_SEEDS       64
typedef struct {
    char      name[CSOS_NAME_LEN];
    char      formula[CSOS_FORMULA_LEN];
    char      compute[CSOS_FORMULA_LEN];
    char      source[CSOS_NAME_LEN];
    double    center;
    double    strength;
    uint32_t  substrate_hash;
    uint32_t  harvest_cycle;
} csos_seed_t;

/* ═══ ORGANISM (container of membranes + seeds) ═══ */
typedef struct {
    csos_membrane_t *membranes[CSOS_MAX_RINGS];
    int              count;
    char             root[256];

    /* Seed bank: Calvin atoms from completed work */
    csos_seed_t      seeds[CSOS_MAX_SEEDS];
    int              seed_count;
} csos_organism_t;

/* ═══ EQUATION-DERIVED OBSERVABLES ═══
 * These replace every hardcoded constant with a function of membrane state.
 * Each traces to a specific equation in the comment. */

/* Gouterman: resonance width bounds from atom population */
double csos_derive_rw_floor(const csos_membrane_t *m);   /* median(atom.rw) */
double csos_derive_rw_ceil(const csos_membrane_t *m);    /* max(atom.rw) */

/* Marcus: error denominator guard from signal variance (λ) */
double csos_derive_error_guard(const csos_atom_t *a);    /* stddev(recent errors) */
double csos_derive_exp_clamp(const csos_atom_t *a);      /* 4λkT natural scale */

/* Forster: motor learning from transfer efficiency E = 1/(1+(r/R0)^6) */
double csos_derive_motor_growth(double strength, int reps); /* Forster E × 1/sqrt(reps) */
double csos_derive_motor_decay(double strength);            /* exp(-1/τ), τ ∝ strength */

/* Mitchell: gradient thresholds from equilibrium */
double csos_derive_tune_threshold(const csos_membrane_t *m); /* gradient/(signals*n) */
double csos_derive_calvin_threshold(const csos_membrane_t *m); /* co2/(gradient+1) */

/* Boyer: decision gate from ATP = flux*n/3 */
double csos_derive_boyer_threshold(int mitchell_n);       /* 1/(n*3) */
int    csos_derive_stuck_cycles(const csos_membrane_t *m); /* 1+log2(motor_count) */

/* Boyer vitality: continuous flux sigmoid instead of buckets */
double csos_derive_boyer_vitality(double speed, int mitchell_n); /* flux/(flux+1) */

/* V14: ATP/NADPH coupling thresholds */
double csos_derive_atp_overflow(const csos_membrane_t *m);  /* MAX_ATOMS × mitchell_n */
double csos_derive_atp_cost_calvin(const csos_membrane_t *m); /* 3 / (1 + co2_pressure) */
double csos_derive_atp_cost_causal(const csos_membrane_t *m); /* 5 / (1 + motor_count) */
double csos_derive_nadph_cost_cf(const csos_membrane_t *m);   /* 1 / sqrt(checks) */

/* V14: Reactor balance and photoprotection */
double csos_derive_reactor_balance(const csos_membrane_t *m);
int    csos_derive_npq_trigger(const csos_membrane_t *m);   /* ATP > overflow? */

/* Vitality EMA: adaptive smoothing 1/sqrt(alive_cycles) */
double csos_derive_vitality_alpha(uint32_t alive_cycles);

/* F->0: regime streak from atom complexity */
int    csos_derive_df_streak_threshold(const csos_membrane_t *m); /* 2+log2(atoms) */
double csos_derive_f_floor(void);                         /* 1/PHOTON_RING */

/* F->0: regime thresholds from error distribution */
double csos_derive_regime_crisis_threshold(const csos_membrane_t *m); /* mean+2σ */
double csos_derive_regime_bear_threshold(const csos_membrane_t *m);   /* mean+σ */

/* F->0: HMM learning rate from observation depth */
double csos_derive_hmm_alpha(int photon_count);           /* 1/sqrt(n) */

/* Spectral: weight from bandwidth (narrower = more informative) */
double csos_derive_spectral_weight(double spec_lo, double spec_hi);

/* Info decay: half-life from spectral midpoint */
double csos_derive_info_half_life(double spec_lo, double spec_hi);

/* Counterfactual: thresholds from statistical significance */
double csos_derive_cf_regime_threshold(int checks);       /* 1/sqrt(checks) */
double csos_derive_cf_boost_threshold(int checks);        /* 1-1/sqrt(checks) */

/* Agent type: quantile-based from motor interval distribution */
void   csos_derive_agent_quantiles(const csos_membrane_t *m,
                                    double *p25, double *p75, double *p99);

/* Regime bias: from F decomposition instead of fixed 0.9/1.05 */
double csos_derive_regime_rw_bias(const csos_membrane_t *m, double base_rw);

/* ═══ THE CORE API ═══ */

/* Lifecycle */
int  csos_organism_init(csos_organism_t *org, const char *root);
void csos_organism_destroy(csos_organism_t *org);
csos_membrane_t *csos_organism_grow(csos_organism_t *org, const char *name);
csos_membrane_t *csos_organism_find(csos_organism_t *org, const char *name);

/* The ONE function — entire photosynthetic process + F->0 guarantee */
csos_photon_t csos_membrane_absorb(csos_membrane_t *mem, double value,
                                    uint32_t substrate_hash, uint8_t protocol);

/* 3-ring cascade (domain -> cockpit -> organism) */
csos_photon_t csos_organism_absorb(csos_organism_t *org, const char *substrate,
                                    const char *raw, uint8_t protocol);

/* Forster coupling */
int  csos_membrane_diffuse(csos_membrane_t *src, csos_membrane_t *tgt);
int  csos_membrane_couple(csos_membrane_t *a, csos_membrane_t *b);
double csos_membrane_coupling_strength(const csos_membrane_t *m, const char *peer);

/* The Living Equation */
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

/* Seed bank */
int  csos_seed_save(const csos_organism_t *org, const char *dir);
int  csos_seed_load(csos_organism_t *org, const char *dir);

/* ═══ SPEC PARSER ═══ */

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

#define CSOS_MAX_SUBSTRATES  16
typedef struct {
    char      name[CSOS_NAME_LEN];
    double    spectral[2];
    double    rw_hint;
    int       calvin_freq_hint;
} csos_substrate_profile_t;

typedef struct {
    csos_spec_atom_t atoms[CSOS_MAX_ATOMS];
    int              atom_count;
    char             ring_names[CSOS_MAX_RINGS][CSOS_NAME_LEN];
    int              ring_mitchell_n[CSOS_MAX_RINGS];
    int              ring_count;
    csos_substrate_profile_t substrates[CSOS_MAX_SUBSTRATES];
    int              substrate_count;
} csos_spec_t;

int  csos_spec_parse(const char *path, csos_spec_t *spec);
const csos_substrate_profile_t *csos_spec_find_substrate(
    const csos_spec_t *spec, const char *name);
int  csos_spec_load_calvin(const char *rings_dir, csos_spec_t *spec);
csos_membrane_t *csos_membrane_from_spec(const csos_spec_t *spec, int ring_index);

/* ═══ FORMULA EVALUATION ═══ */
double csos_formula_eval(const char *compute, const double *params,
                         const char param_keys[][32], int param_count,
                         double signal);

#endif /* CSOS_MEMBRANE_H */
