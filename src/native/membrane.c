/*
 * CSOS Membrane — The unified photosynthetic process.
 * F->0 architecture: pure physics, no session/event/message layer.
 *
 * ONE function: membrane_absorb()
 * ONE data flow: Photon → Gouterman → Marcus → Mitchell → Boyer → Calvin
 * ONE struct: everything travels in the unified Photon
 *
 * This replaces: physics.c + transport.c + agent.c + gateway.c absorb
 * No layers. No message passing. No serialization between stages.
 *
 * The membrane IS the Universal IR:
 *   Input:  (value, substrate_hash, protocol)
 *   Output: csos_photon_t with ALL fields populated
 *   LLM reads the photon. Membrane computes the photon.
 *   Gradient is the shared language.
 */
#include "../../lib/membrane.h"
#include <math.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>

/* Perf counters + autotune state (read by protocol.c perf action) */
uint64_t _absorb_ns_total = 0;
uint32_t _absorb_count = 0;
int _autotune_calvin_freq = 2;       /* Initial; converges via CO2 pressure */
int _autotune_compact_freq = 100;

/* ═══ EQUATION-DERIVED OBSERVABLES (Law I: no magic numbers) ═══
 * Every function below replaces a former #define with a computation
 * rooted in the 5 equations and the membrane's own running state. */

/* ── Gouterman: RW bounds from atom population ── */
double csos_derive_rw_floor(const csos_membrane_t *m) {
    /* median(atom.rw): the population's typical selectivity */
    if (m->atom_count == 0) return 0.85;  /* bootstrap: dof=5 → 5/6 ≈ 0.833 */
    double vals[CSOS_MAX_ATOMS];
    int n = m->atom_count;
    for (int i = 0; i < n; i++) vals[i] = m->atoms[i].rw;
    /* Simple insertion sort for small n (≤32) */
    for (int i = 1; i < n; i++) {
        double v = vals[i]; int j = i;
        while (j > 0 && vals[j-1] > v) { vals[j] = vals[j-1]; j--; }
        vals[j] = v;
    }
    return (n % 2 == 0) ? (vals[n/2-1] + vals[n/2]) / 2.0 : vals[n/2];
}

double csos_derive_rw_ceil(const csos_membrane_t *m) {
    /* max(atom.rw): most complex atom sets the ceiling */
    if (m->atom_count == 0) return 0.92;  /* bootstrap: dof=11 → 11/12 ≈ 0.917 */
    double mx = 0;
    for (int i = 0; i < m->atom_count; i++)
        if (m->atoms[i].rw > mx) mx = m->atoms[i].rw;
    return mx > 0.999 ? 0.999 : mx;  /* hard ceiling at 1.0 is mathematical, not magic */
}

/* ── Marcus: error guard from signal variance (λ = reorganization energy) ── */
double csos_derive_error_guard(const csos_atom_t *a) {
    /* stddev of recent errors — Marcus λ: the natural error scale */
    if (a->photon_count < 3) return 0.01;  /* bootstrap guard */
    int n = a->photon_count < a->photon_cap ? a->photon_count : a->photon_cap;
    int w = n < 20 ? n : 20;
    double sum = 0, sum2 = 0;
    for (int i = 0; i < w; i++) {
        int idx = (a->photon_head - 1 - i) & (a->photon_cap - 1);
        double e = a->photons[idx].error;
        sum += e; sum2 += e * e;
    }
    double mean = sum / w;
    double var = sum2 / w - mean * mean;
    if (var < 0) var = 0;
    double sd = sqrt(var);
    return sd > 1e-10 ? sd : 1e-10;  /* floor is numerical epsilon, not magic */
}

double csos_derive_exp_clamp(const csos_atom_t *a) {
    /* 4λkT from Marcus: the natural scale at which exp() saturates.
     * λ = error_guard (stddev), kT = 1 (normalized). */
    double lambda = csos_derive_error_guard(a);
    return 4.0 * lambda + 1.0;  /* +1 prevents clamp < 1 at bootstrap */
}

/* ── Forster: motor coupling from transfer efficiency ── */
double csos_derive_motor_growth(double strength, int reps) {
    /* E = 1/(1+(r/R0)^6) where r = 1-strength (distance = inexperience)
     * Scaled by 1/sqrt(reps) — Mitchell-derived diminishing returns */
    double r = 1.0 - strength;
    if (r < 1e-10) r = 1e-10;
    double r6 = r * r * r * r * r * r;  /* r^6 */
    double E = 1.0 / (1.0 + r6);       /* Forster transfer efficiency */
    double lr = 1.0 / (1.0 + sqrt((double)(reps > 0 ? reps : 1)));
    return E * lr;
}

double csos_derive_motor_decay(double strength) {
    /* exp(-1/τ) where τ = 1/(1-strength+ε): strong coupling = slow decay */
    double tau = 1.0 / (1.0 - strength + 0.01);
    double d = exp(-1.0 / tau);
    /* Natural bounds: at strength=0 → τ≈0.99 → decay≈0.366
     *                 at strength=1 → τ=100  → decay≈0.990 */
    return d;
}

/* ── Mitchell: gradient equilibrium thresholds ── */
double csos_derive_tune_threshold(const csos_membrane_t *m) {
    /* At equilibrium: ΔG = 0 → nFΔψ = 2.3RT·ΔpH
     * threshold = gradient / (signals * n + 1): normalized gradient per signal */
    if (m->signals == 0) return 0.1;  /* bootstrap */
    double equil = m->gradient / ((double)m->signals * m->mitchell_n + 1.0);
    return equil > 1e-10 ? equil : 1e-10;
}

double csos_derive_calvin_threshold(const csos_membrane_t *m) {
    /* Calvin fires when CO2 pressure exceeds absorption efficiency:
     * threshold = co2_count / (gradient + 1) */
    return (double)m->co2_count / (m->gradient + 1.0);
}

/* ── Boyer: decision gate from ATP = flux * n / 3 ── */
double csos_derive_boyer_threshold(int mitchell_n) {
    /* ATP synthesis requires flux*n/3 ≥ 1 ATP → action_ratio > 1/(n*3)
     * eco_domain (n=1): 0.333, eco_cockpit (n=2): 0.167, eco_organism (n=3): 0.111 */
    return 1.0 / ((double)mitchell_n * 3.0);
}

int csos_derive_stuck_cycles(const csos_membrane_t *m) {
    /* Patience scales with memory depth: more history = more patience */
    int mc = m->motor_count > 0 ? m->motor_count : 1;
    return 1 + (int)(log2((double)mc));
}

double csos_derive_boyer_vitality(double speed, int mitchell_n) {
    /* Continuous flux sigmoid: flux/(flux+1) where flux = speed * n
     * Replaces hardcoded EXPLORE_HIGH/LOW/STUCK buckets */
    double flux = speed * (double)mitchell_n;
    return flux / (flux + 1.0);
}

double csos_derive_vitality_alpha(uint32_t alive_cycles) {
    /* 1/sqrt(alive_cycles+1): fast at start, stabilizes over time.
     * Classic adaptive EMA — no hardcoded smoothing factor. */
    return 1.0 / (1.0 + sqrt((double)(alive_cycles > 0 ? alive_cycles : 1)));
}

/* ── F->0: regime streak from atom complexity ── */
int csos_derive_df_streak_threshold(const csos_membrane_t *m) {
    /* log2(atom_count): more atoms = more inertia before regime rebuild */
    int ac = m->atom_count > 0 ? m->atom_count : 1;
    return 2 + (int)(log2((double)ac));
}

double csos_derive_f_floor(void) {
    /* 1/PHOTON_RING: resolution limit of the observation buffer */
    return 1.0 / (double)CSOS_PHOTON_RING;
}

/* ── F->0: regime thresholds from running error distribution ── */
static void _running_error_stats(const csos_membrane_t *m, double *mean_out, double *std_out) {
    int n = m->f_count < 50 ? m->f_count : 50;
    if (n < 2) { *mean_out = 0.5; *std_out = 0.2; return; } /* bootstrap */
    double sum = 0, sum2 = 0;
    for (int i = m->f_count - n; i < m->f_count; i++) {
        sum += m->f_history[i]; sum2 += m->f_history[i] * m->f_history[i];
    }
    *mean_out = sum / n;
    double var = sum2 / n - (*mean_out) * (*mean_out);
    *std_out = var > 0 ? sqrt(var) : 0.1;
}

double csos_derive_regime_crisis_threshold(const csos_membrane_t *m) {
    double mean, sd; _running_error_stats(m, &mean, &sd);
    return mean + 2.0 * sd;  /* 2-sigma event */
}

double csos_derive_regime_bear_threshold(const csos_membrane_t *m) {
    double mean, sd; _running_error_stats(m, &mean, &sd);
    return mean + sd;         /* 1-sigma event */
}

double csos_derive_hmm_alpha(int photon_count) {
    /* 1/sqrt(n): Bayesian update rate — more observations = smaller updates */
    return 1.0 / (1.0 + sqrt((double)(photon_count > 0 ? photon_count : 1)));
}

double csos_derive_spectral_weight(double spec_lo, double spec_hi) {
    /* 1/sqrt(bandwidth): narrower band = more informative per Hz */
    double bw = spec_hi - spec_lo;
    if (bw < 1.0) bw = 1.0;
    return 1.0 / sqrt(bw / 1000.0);  /* normalized to ~1.0 for 1000-wide band */
}

double csos_derive_info_half_life(double spec_lo, double spec_hi) {
    /* spectral_midpoint / 10: the spectral range defines the timescale.
     * HF (mid=50): 5 cycles. MF (mid=500): 50 cycles. LF (mid=2500): 250 cycles. */
    double mid = (spec_lo + spec_hi) / 2.0;
    double hl = mid / 10.0;
    return hl > 1.0 ? hl : 1.0;  /* minimum 1 cycle */
}

double csos_derive_cf_regime_threshold(int checks) {
    /* Statistical significance: below 1/sqrt(checks) = not enough evidence for direction */
    if (checks < 1) return 0.5;
    return 1.0 / sqrt((double)checks);
}

double csos_derive_cf_boost_threshold(int checks) {
    /* Complement: above 1-1/sqrt(checks) = statistically significant accuracy */
    if (checks < 1) return 0.5;
    return 1.0 - 1.0 / sqrt((double)checks);
}

void csos_derive_agent_quantiles(const csos_membrane_t *m,
                                  double *p25, double *p75, double *p99) {
    /* Quantile-based agent classification from motor interval distribution */
    if (m->motor_count < 4) { *p25 = 5; *p75 = 50; *p99 = 500; return; }
    uint64_t intervals[CSOS_MAX_MOTOR];
    int n = 0;
    for (int i = 0; i < m->motor_count; i++) {
        if (m->motor[i].interval > 0 && m->motor[i].reps >= 2)
            intervals[n++] = m->motor[i].interval;
    }
    if (n < 4) { *p25 = 5; *p75 = 50; *p99 = 500; return; }
    /* Simple insertion sort */
    for (int i = 1; i < n; i++) {
        uint64_t v = intervals[i]; int j = i;
        while (j > 0 && intervals[j-1] > v) { intervals[j] = intervals[j-1]; j--; }
        intervals[j] = v;
    }
    *p25 = (double)intervals[n / 4];
    *p75 = (double)intervals[(3 * n) / 4];
    *p99 = (double)intervals[n - 1 - n / 100];
}

/* V14: ATP overflow threshold — when light reactor overproduces */
double csos_derive_atp_overflow(const csos_membrane_t *m) {
    return (double)CSOS_MAX_ATOMS * (double)(m->mitchell_n > 0 ? m->mitchell_n : 1);
}

/* V14: Reactor balance from production/consumption ratio */
double csos_derive_reactor_balance(const csos_membrane_t *m) {
    double prod = m->atp_produced_this_cycle + m->nadph_produced_this_cycle + 1e-10;
    double cons = m->atp_consumed_this_cycle + m->nadph_consumed_this_cycle + 1e-10;
    return prod / cons;
}

/* V14: Should photoprotection trigger? */
int csos_derive_npq_trigger(const csos_membrane_t *m) {
    return m->atp_pool > csos_derive_atp_overflow(m);
}

double csos_derive_regime_rw_bias(const csos_membrane_t *m, double base_rw) {
    /* Use F_free_energy instead of fixed 0.9/1.05 multipliers.
     * F_free_energy < 0 → system ahead → can afford to narrow (be selective)
     * F_free_energy > 0 → system behind → widen to absorb more
     * bias = 1 + F_free_energy * 0.1 (damped to prevent oscillation) */
    double bias = 1.0 + m->F_free_energy * 0.1;
    if (bias < 0.85) bias = 0.85;  /* don't narrow more than 15% */
    if (bias > 1.15) bias = 1.15;  /* don't widen more than 15% */
    double result = base_rw * bias;
    if (result > 0.999) result = 0.999;
    return result;
}

/* ═══ EQUATIONS ═══
 * NO hardcoded array. All atoms loaded from specs/eco.csos via csos_spec_parse().
 * Foundation atoms (Gouterman, Forster, Marcus, Mitchell, Boyer) and Calvin atoms
 * are all parsed from spec files — the spec IS the code.
 *
 * See: csos_membrane_from_spec() in spec_parse.c
 *      csos_formula_eval() in formula_eval.c
 */

/* ═══ ATOM OPERATIONS ═══ */

void csos_atom_init(csos_atom_t *a, const char *name, const char *formula,
                    const char *source, const char **param_keys, int param_count) {
    memset(a, 0, sizeof(*a));
    strncpy(a->name, name, CSOS_NAME_LEN - 1);
    strncpy(a->formula, formula, CSOS_FORMULA_LEN - 1);
    strncpy(a->source, source, CSOS_NAME_LEN - 1);
    a->param_count = param_count;
    for (int i = 0; i < param_count && i < CSOS_MAX_PARAMS; i++) {
        strncpy(a->param_keys[i], param_keys[i], 31);
        a->params[i] = 1.0;
    }
    /* Default spectral range — overridden by spec_parse for spec-loaded atoms */
    a->spectral[0] = 0;
    a->spectral[1] = 10000;
    a->broadband = 0;
    a->photon_cap = CSOS_ATOM_PHOTON_RING;
    a->photons = (csos_photon_t *)calloc(CSOS_ATOM_PHOTON_RING, sizeof(csos_photon_t));
    a->photon_head = 0;
    a->local_cap = CSOS_ATOM_PHOTON_RING;
    a->local_photons = (csos_photon_t *)calloc(CSOS_ATOM_PHOTON_RING, sizeof(csos_photon_t));
    a->local_head = 0;
    a->has_resonated = 0;
    a->last_resonated_value = 0;
    csos_atom_compute_rw(a);
}

void csos_atom_compute_rw(csos_atom_t *a) {
    /* Gouterman-derived: DOF from formula complexity + parameters + limits.
     * Formula complexity = strlen/mean_formula_len (self-normalizing).
     * rw = dof/(dof+1) → asymptotic approach to 1.0 as complexity grows. */
    int flen = (int)strlen(a->formula);
    int fc = flen > 0 ? (flen / (flen / (a->param_count + 1) + 1)) : 1;
    if (fc < 1) fc = 1;
    int dof = a->param_count + a->limit_count + fc;
    a->rw = (double)dof / (double)(dof + 1);
}

static void atom_record(csos_atom_t *a, const csos_photon_t *ph) {
    a->photons[a->photon_head & (a->photon_cap - 1)] = *ph;
    a->photon_head++;
    a->photon_count++;
}
static void atom_record_local(csos_atom_t *a, const csos_photon_t *ph) {
    a->local_photons[a->local_head & (a->local_cap - 1)] = *ph;
    a->local_head++;
    a->local_count++;
}

/* Incremental gradient: updated in absorb loop, avoids O(n) scan.
 * Falls back to full scan only on first call for a fresh atom. */
static double atom_gradient(const csos_atom_t *a) {
    int g = 0;
    int count = a->photon_count < a->photon_cap ? a->photon_count : a->photon_cap;
    for (int i = 0; i < count; i++)
        if (a->photons[i].resonated) g++;
    return (double)g;
}

/* atom_F with pre-computed gradient to avoid double-scan */
static double atom_F_cached(const csos_atom_t *a, int cached_grad) {
    if (a->photon_count == 0) return 1.0;
    /* Gouterman-derived window: rw bounds the relevant observation depth.
     * dE = hc/λ → higher rw (more degrees of freedom) = wider relevant window.
     * Window = max(gradient+1, rw * photon_count), clamped to photon_count. */
    int rw_window = (int)(a->rw * a->photon_count);
    int w = cached_grad + 1;
    if (rw_window > w) w = rw_window;
    if (w > a->photon_count) w = a->photon_count;
    if (w < 1) w = 1;
    int ring_count = a->photon_count < a->photon_cap ? a->photon_count : a->photon_cap;
    if (w > ring_count) w = ring_count;
    double sum = 0; int n = 0;
    for (int i = 0; i < w; i++) {
        int idx = (a->photon_head - w + i) & (a->photon_cap - 1);
        if (idx < 0) idx += a->photon_cap;
        sum += a->photons[idx].error;
        n++;
    }
    return n > 0 ? sum / n : 1.0;
}

/* atom_tune with pre-computed gradient to avoid triple-scan.
 * m_tune_thresh: Mitchell-derived equilibrium threshold from membrane. */
static void atom_tune_cached(csos_atom_t *a, int cached_grad, double m_tune_thresh) {
    if (a->photon_count < 3) return;
    int g = cached_grad;
    int w = (g > 0 ? g : 1);
    int ring_count = a->photon_count < a->photon_cap ? a->photon_count : a->photon_cap;
    if (w > ring_count) w = ring_count;
    double bias = 0;
    for (int i = 0; i < w; i++) {
        int idx = (a->photon_head - w + i) & (a->photon_cap - 1);
        if (idx < 0) idx += a->photon_cap;
        bias += a->photons[idx].predicted - a->photons[idx].actual;
    }
    bias /= w;
    /* Mitchell equilibrium: only tune when bias exceeds gradient-per-signal.
     * Error guard from Marcus λ (signal variance), not a fixed constant. */
    double tune_equil = m_tune_thresh > 0 ? m_tune_thresh : 0.1;
    if (fabs(bias) < a->rw * tune_equil) return;
    double lr = 1.0 / (1.0 + g);
    for (int k = 0; k < a->param_count; k++) {
        double pv = fabs(a->params[k]);
        double guard = csos_derive_error_guard(a);
        if (pv == 0) pv = guard;
        a->params[k] -= bias * lr * pv;
    }
}

/* ═══ MEMBRANE LIFECYCLE ═══ */

/*
 * csos_membrane_create — spec-driven membrane initialization.
 *
 * Tries to load atoms from specs/eco.csos (the genome).
 * If spec file not found, creates an empty membrane.
 * The spec IS the code — no hardcoded equation array.
 *
 * For full spec-driven creation with ring selection, use
 * csos_membrane_from_spec() in spec_parse.c instead.
 */
csos_membrane_t *csos_membrane_create(const char *name) {
    csos_membrane_t *m = (csos_membrane_t *)calloc(1, sizeof(csos_membrane_t));
    if (!m) return NULL;
    strncpy(m->name, name, CSOS_NAME_LEN - 1);
    m->human_present = 1;
    m->mitchell_n = 1; /* Default; overridden by csos_membrane_from_spec() if spec provides it */

    /* Try to load from spec file (the single source of truth) */
    csos_spec_t spec = {0};
    const char *spec_paths[] = {"specs/eco.csos", "../specs/eco.csos", NULL};
    int loaded = 0;
    for (int i = 0; spec_paths[i]; i++) {
        if (csos_spec_parse(spec_paths[i], &spec) == 0 && spec.atom_count > 0) {
            loaded = 1;
            break;
        }
    }

    if (loaded) {
        /* Set mitchell_n from spec ring definition (no hardcoded per-ring logic).
         * Match membrane name to spec ring names and apply configured n. */
        for (int ri = 0; ri < spec.ring_count; ri++) {
            if (strcmp(spec.ring_names[ri], name) == 0) {
                m->mitchell_n = spec.ring_mitchell_n[ri];
                break;
            }
        }

        /* Initialize atoms from parsed spec — generic, no name-based logic */
        int count = 0;
        for (int i = 0; i < spec.atom_count && count < CSOS_MAX_ATOMS; i++) {
            const csos_spec_atom_t *sa = &spec.atoms[i];
            if (strncmp(sa->name, "calvin_", 7) == 0) continue;

            csos_atom_t *a = &m->atoms[count];
            memset(a, 0, sizeof(*a));
            strncpy(a->name, sa->name, CSOS_NAME_LEN - 1);
            strncpy(a->formula, sa->formula, CSOS_FORMULA_LEN - 1);
            strncpy(a->compute, sa->compute, CSOS_FORMULA_LEN - 1);
            strncpy(a->source, sa->source, CSOS_NAME_LEN - 1);
            strncpy(a->born_in, name, CSOS_NAME_LEN - 1);
            a->param_count = sa->param_count;
            for (int j = 0; j < sa->param_count; j++) {
                strncpy(a->param_keys[j], sa->param_keys[j], 31);
                a->params[j] = sa->param_defaults[j];
            }
            a->spectral[0] = sa->spectral[0];
            a->spectral[1] = sa->spectral[1];
            a->broadband = sa->broadband;
            a->photon_cap = CSOS_ATOM_PHOTON_RING;
            a->photons = (csos_photon_t *)calloc(CSOS_ATOM_PHOTON_RING, sizeof(csos_photon_t));
            a->photon_head = 0;
            a->local_cap = CSOS_ATOM_PHOTON_RING;
            a->local_photons = (csos_photon_t *)calloc(CSOS_ATOM_PHOTON_RING, sizeof(csos_photon_t));
            a->local_head = 0;
            a->has_resonated = 0;
            a->last_resonated_value = 0;
            csos_atom_compute_rw(a);
            count++;
        }
        m->atom_count = count;
    }

    /* Dynamic RW: start at population floor, adapts via F in absorb */
    m->rw = csos_derive_rw_floor(m);

    /* V14: Initialize dual-reactor coupling state.
     * ATP/NADPH pools start at FOUNDATION_ATOMS (enough for one dark cycle).
     * reactor_balance starts at 1.0 (balanced). */
    m->atp_pool = (double)CSOS_FOUNDATION_ATOMS;
    m->nadph_pool = (double)CSOS_FOUNDATION_ATOMS;
    m->reactor_balance = 1.0;
    m->npq_active = 0;
    m->npq_dissipated = 0;

#ifdef CSOS_HAS_LLVM
    /* Compile all formula expressions to native code via LLVM */
    if (m->atom_count > 0) csos_formula_jit_compile(m);
#endif

    return m;
}

static void membrane_free_atoms(csos_membrane_t *m) {
    for (int i = 0; i < m->atom_count; i++) {
        free(m->atoms[i].photons);
        free(m->atoms[i].local_photons);
    }
}

/* ═══ MOTOR MEMORY (embedded in membrane) ═══ */

static csos_motor_t *motor_find(csos_membrane_t *m, uint32_t hash) {
    for (int i = 0; i < m->motor_count; i++)
        if (m->motor[i].substrate_hash == hash) return &m->motor[i];
    if (m->motor_count < CSOS_MAX_MOTOR)
        return &m->motor[m->motor_count++];
    /* Evict weakest */
    int w = 0;
    for (int i = 1; i < m->motor_count; i++)
        if (m->motor[i].strength < m->motor[w].strength) w = i;
    m->motor[w] = (csos_motor_t){0};
    return &m->motor[w];
}

static void motor_update(csos_membrane_t *m, uint32_t hash,
                         double *out_strength, uint64_t *out_interval) {
    csos_motor_t *e = motor_find(m, hash);
    if (e->substrate_hash == 0) {
        e->substrate_hash = hash;
        e->last_seen = m->motor_cycle;
        e->reps = 1;
        /* Forster: first encounter = max transfer efficiency at distance 1.0 */
        double init_growth = csos_derive_motor_growth(0.0, 1);
        e->strength = init_growth;
        *out_strength = init_growth;
        *out_interval = 0;
    } else {
        uint64_t ni = m->motor_cycle - e->last_seen;
        e->prev_interval = e->interval;
        e->interval = ni;
        e->last_seen = m->motor_cycle;
        e->reps++;
        if (ni > e->prev_interval && e->prev_interval > 0) {
            /* Spacing bonus: Forster growth × spacing factor.
             * Spacing factor capped by log2(sf) to prevent runaway. */
            double sf = (double)ni / (double)(e->prev_interval + 1);
            double max_sf = 1.0 + log2((double)(e->reps > 1 ? e->reps : 2));
            if (sf > max_sf) sf = max_sf;
            e->strength += csos_derive_motor_growth(e->strength, e->reps) * sf;
        } else if (ni > 0) {
            /* Forster transfer efficiency: E = 1/(1+(r/R0)^6)
             * r = 1-strength (distance = inexperience).
             * Growth naturally diminishes as coupling strengthens. */
            double growth = csos_derive_motor_growth(e->strength, e->reps);
            e->strength += growth;
        }
        /* Forster-derived decay: exp(-1/τ) where τ ∝ coupling strength.
         * Strong coupling = long memory. Weak = fast forgetting. */
        double decay = csos_derive_motor_decay(e->strength);
        e->strength *= decay;
        if (e->strength > 1.0) e->strength = 1.0;
        *out_strength = e->strength;
        *out_interval = ni;
    }
    m->motor_cycle++;
}

double csos_motor_strength(const csos_membrane_t *m, uint32_t hash) {
    for (int i = 0; i < m->motor_count; i++)
        if (m->motor[i].substrate_hash == hash) return m->motor[i].strength;
    return 0.0;
}

int csos_motor_top(const csos_membrane_t *m, uint32_t *hashes,
                   double *strengths, int max) {
    int c = 0;
    for (int i = 0; i < m->motor_count && c < max; i++) {
        if (m->motor[i].substrate_hash == 0) continue;
        int pos = c;
        while (pos > 0 && m->motor[i].strength > strengths[pos - 1]) pos--;
        if (pos < max) {
            for (int j = (c < max - 1 ? c : max - 2); j > pos; j--) {
                hashes[j] = hashes[j-1]; strengths[j] = strengths[j-1];
            }
            hashes[pos] = m->motor[i].substrate_hash;
            strengths[pos] = m->motor[i].strength;
            if (c < max) c++;
        }
    }
    return c;
}

/* ═══ CALVIN CYCLE (pattern synthesis from non-resonated signals) ═══ */

static int membrane_calvin(csos_membrane_t *m) {
    if (m->co2_count < 5) return 0;
    int n = m->co2_count < CSOS_CALVIN_SAMPLE_SIZE ? m->co2_count : CSOS_CALVIN_SAMPLE_SIZE;
    int start = m->co2_count - n;

    double mean = 0;
    for (int i = start; i < m->co2_count; i++) mean += m->co2[i];
    mean /= n;

    double var = 0;
    if (n > 1) {
        for (int i = start; i < m->co2_count; i++)
            var += (m->co2[i] - mean) * (m->co2[i] - mean);
        var /= (n - 1);
    }

    /* Check local gradient threshold */
    int local_grad = 0;
    for (int i = 0; i < m->atom_count; i++) {
        int lc = m->atoms[i].local_count < m->atoms[i].local_cap ? m->atoms[i].local_count : m->atoms[i].local_cap;
        for (int j = 0; j < lc; j++)
            if (m->atoms[i].local_photons[j].resonated) local_grad++;
    }
    double co2_mean_full = 0;
    for (int i = 0; i < m->co2_count; i++) co2_mean_full += m->co2[i];
    if (m->co2_count > 0) co2_mean_full /= m->co2_count;
    /* Mitchell-derived: Calvin fires when CO2 pressure exceeds absorption efficiency.
     * threshold = co2_count/(gradient+1) — emergent, not a fixed fraction. */
    double calvin_thresh = csos_derive_calvin_threshold(m);
    double thresh = co2_mean_full * (calvin_thresh > 0 ? 1.0 / (1.0 + calvin_thresh) : 0.05);
    if (thresh < 1e-10) thresh = 1e-10;
    if (local_grad < thresh) return 0;

    /* Check if pattern already captured */
    for (int i = 0; i < m->atom_count; i++) {
        csos_atom_t *ex = &m->atoms[i];
        if (ex->local_count == 0) continue;
        double rs = 0; int rn = 0;
        int lcount = ex->local_count < ex->local_cap ? ex->local_count : ex->local_cap;
        for (int j = 0; j < lcount && rn < CSOS_CALVIN_MATCH_DEPTH; j++) {
            int idx = (ex->local_head - 1 - j) & (ex->local_cap - 1);
            if (ex->local_photons[idx].resonated) { rs += ex->local_photons[idx].actual; rn++; }
        }
        if (rn == 0) continue;
        double em = rs / rn;
        /* Variance tolerance: use atom's own rw as the natural scale —
         * no separate CALVIN_VAR_MULT constant needed. */
        double vt = sqrt(var) * (1.0 / (1.0 + ex->param_count));
        double t = ex->rw > vt ? ex->rw : vt;
        double ea = fabs(em); if (ea < 1e-10) ea = 1e-10;
        if (fabs(mean - em) / ea < t) return 0;
    }

    if (m->atom_count >= CSOS_MAX_ATOMS) return 0;

    /* Synthesize new atom — carries its own compute expression (Law I) */
    csos_atom_t *na = &m->atoms[m->atom_count];
    memset(na, 0, sizeof(csos_atom_t));
    snprintf(na->name, CSOS_NAME_LEN, "calvin_c%u", m->cycles);
    snprintf(na->formula, CSOS_FORMULA_LEN, "pattern@%.1f+/-%.1f", mean, sqrt(var));
    /* Compute expression: constant prediction from synthesized center value */
    snprintf(na->compute, CSOS_FORMULA_LEN, "%.6f", mean);
    snprintf(na->source, CSOS_NAME_LEN, "Calvin %s", m->name);
    strncpy(na->born_in, m->name, CSOS_NAME_LEN - 1);
    na->params[0] = mean; na->param_count = 1;
    strncpy(na->param_keys[0], "c", 31);
    na->spectral[0] = 0; na->spectral[1] = 10000;
    na->broadband = 0;
    na->photon_cap = CSOS_ATOM_PHOTON_RING;
    na->photons = (csos_photon_t *)calloc(CSOS_ATOM_PHOTON_RING, sizeof(csos_photon_t));
    na->photon_head = 0;
    na->local_cap = CSOS_ATOM_PHOTON_RING;
    na->local_photons = (csos_photon_t *)calloc(CSOS_ATOM_PHOTON_RING, sizeof(csos_photon_t));
    na->local_head = 0;
    na->has_resonated = 0;
    na->last_resonated_value = 0;
    csos_atom_compute_rw(na);
    m->atom_count++;
    m->co2_count = 0;

#ifdef CSOS_HAS_LLVM
    /* Hot-reload: recompile all formulas including the new Calvin atom */
    csos_formula_jit_check(m);
#endif

    return 1;
}

/* Forward declarations: free energy functions (defined in freeenergy.c) */
static uint32_t freeenergy_post_absorb(csos_membrane_t *m, const csos_photon_t *ph);
static int dark_causal_inject(csos_membrane_t *m, const csos_photon_t *ph);

/* ═══ membrane_absorb: THE ENTIRE PHOTOSYNTHETIC PROCESS ═══ */

csos_photon_t csos_membrane_absorb(csos_membrane_t *m, double value,
                                    uint32_t substrate_hash, uint8_t protocol) {
    csos_photon_t ph = {0};
    ph.actual = value;
    ph.substrate_hash = substrate_hash;
    ph.protocol = protocol;
    ph.cycle = m->cycles;

    /* ── MOTOR TRACE (spaced repetition — before physics) ── */
    motor_update(m, substrate_hash, &ph.motor_strength, &ph.interval);

    /* ── GOUTERMAN + MARCUS (resonance check, per atom, with regime bias) ── */
    int produced = 0;
    for (int i = 0; i < m->atom_count; i++) {
        csos_atom_t *a = &m->atoms[i];

        /* Predict from cached last-resonated value (O(1) vs O(n) scan) */
        double pred = a->has_resonated ? a->last_resonated_value : value;

        /* Marcus: error = |predicted - actual| / max(|actual|, |predicted|*λ+ε)
         * λ (error guard) derived from atom's own error variance — not a fixed 0.01. */
        double denom = fabs(value);
        double guard = csos_derive_error_guard(a);
        double alt = fabs(pred) * guard + 1e-10;
        if (alt > denom) denom = alt;
        double error = fabs(pred - value) / denom;

        /* F->0 Regime bias: derived from F_free_energy decomposition.
         * F_free_energy < 0 → system ahead → narrower (selective).
         * F_free_energy > 0 → system behind → wider (exploratory). */
        double effective_rw = csos_derive_regime_rw_bias(m, a->rw);
        int resonated = (error < effective_rw) ? 1 : 0;

        /* Record on atom (unified photon carries context) */
        csos_photon_t ap = ph;  /* Copy base fields (motor context etc) */
        ap.predicted = pred;
        ap.error = error;
        ap.resonated = resonated;

        atom_record(a, &ap);
        atom_record_local(a, &ap);

        if (resonated) produced++;
        if (resonated) { a->last_resonated_value = value; a->has_resonated = 1; }

        /* Calvin CO2 pool */
        if (!resonated && m->co2_count < CSOS_CO2_POOL_SIZE)
            m->co2[m->co2_count++] = value;
    }

    /* ── CAUSAL INJECT (F->0: predict effects BEFORE they manifest) ── */
    /* When a causal atom resonates, inject predicted signal for its target.
     * This builds gradient from predictions, not just observations.
     * Boyer can EXECUTE before the effect arrives. Time advantage = causal lag. */
    dark_causal_inject(m, &ph);

    /* ── MITCHELL (gradient update) ── */
    /* ΔG = -n·F·Δψ: gradient accumulates by n * resonated_count.
     * n (mitchell_n) is loaded from spec per ring:
     *   eco_domain n=1: each resonated photon adds 1 to gradient
     *   eco_cockpit n=2: meta-signals are worth 2 each
     *   eco_organism n=3: integration signals worth 3 (full ATP cycle)
     * This differentiates ring specificity without magic numbers. */
    double g0 = m->gradient;
    m->gradient += produced * m->mitchell_n;
    m->signals++;
    ph.delta = (int32_t)(m->gradient - g0);
    ph.resonated = (produced > 0);
    ph.calvin_candidate = (produced == 0);

    /* Update derived metrics — single-pass with cached gradients.
     * Cache per-atom gradient count to avoid O(n) rescans in F and tune. */
    double total = 0;
    int atom_grads[CSOS_MAX_ATOMS];
    for (int i = 0; i < m->atom_count; i++) {
        total += m->atoms[i].photon_count;
        atom_grads[i] = (int)atom_gradient(&m->atoms[i]);
    }
    m->speed = total > 0 ? m->gradient / total : 0;
    m->F = 0;
    for (int i = 0; i < m->atom_count; i++)
        m->F += atom_F_cached(&m->atoms[i], atom_grads[i]);
    if (m->atom_count > 0) m->F /= m->atom_count;
    /* F floor: 1/PHOTON_RING — the observation buffer's resolution limit. */
    double f_floor = csos_derive_f_floor();
    if (m->F < f_floor) m->F = f_floor;
    m->action_ratio = total > 0 ? m->gradient / total : 0;
    if (m->f_count < CSOS_FHIST_LEN) m->f_history[m->f_count++] = m->F;

    /* Gouterman-derived dynamic resonance width:
     * FLOOR and CEIL from atom population (median and max rw).
     * rw = floor + (ceil - floor) * F/(F+1) */
    double rw_floor = csos_derive_rw_floor(m);
    double rw_ceil  = csos_derive_rw_ceil(m);
    m->rw = rw_floor + (rw_ceil - rw_floor) * m->F / (m->F + 1.0);

    /* ── TUNE (Marcus error correction — when F rising) ── */
    double tune_thresh = csos_derive_tune_threshold(m);
    if (m->f_count >= 2 && m->f_history[m->f_count-1] > m->f_history[m->f_count-2]) {
        for (int i = 0; i < m->atom_count; i++)
            atom_tune_cached(&m->atoms[i], atom_grads[i], tune_thresh);
    }

    /* ── BOYER (decision gate: ATP = flux*n/3) ── */
    double boyer_thresh = csos_derive_boyer_threshold(m->mitchell_n);
    int stuck_limit = csos_derive_stuck_cycles(m);
    if (m->speed > m->rw) {
        ph.decision = DECISION_EXECUTE;
        if (m->mode == MODE_PLAN) m->mode = MODE_BUILD;
    } else if (m->action_ratio > boyer_thresh && ph.delta > 0) {
        ph.decision = DECISION_EXPLORE;
    } else {
        if (ph.delta == 0) m->consecutive_zero_delta++;
        else m->consecutive_zero_delta = 0;
        if (m->consecutive_zero_delta >= stuck_limit || m->action_ratio < boyer_thresh)
            ph.decision = m->human_present ? DECISION_ASK : DECISION_STORE;
        else
            ph.decision = DECISION_EXPLORE;
    }
    m->decision = ph.decision;

    /* ── VITALITY (the unified living equation — all 5 contributions) ── */
    {
        csos_equation_t *eq = &m->equation;

        /* Gouterman: resonance ratio — what fraction of signals match? */
        eq->gouterman = m->signals > 0 ? m->gradient / (m->signals * m->mitchell_n + 1e-10) : 0;
        if (eq->gouterman > 1.0) eq->gouterman = 1.0;

        /* Forster: coupling health — mean coupling strength to peers (0 if isolated) */
        if (m->coupling_count > 0) {
            double cs = 0;
            for (int i = 0; i < m->coupling_count; i++)
                cs += m->couplings[i].coupling;
            eq->forster = cs / m->coupling_count;
            /* Normalize: coupling can be huge (1/r^6), map through sigmoid */
            eq->forster = eq->forster / (eq->forster + 1.0);
        } else {
            eq->forster = 0.1; /* Isolated membrane has minimal Forster contribution */
        }

        /* Marcus: prediction accuracy — 1 - normalized_F.
         * F is rolling error. High F = inaccurate. Map through sigmoid. */
        eq->marcus = 1.0 / (1.0 + m->F);

        /* Mitchell: life force rate — speed normalized.
         * speed = gradient/total_photons. Map through sigmoid for [0,1]. */
        eq->mitchell = m->speed / (m->speed + 1.0);

        /* Boyer: decision clarity — continuous flux sigmoid.
         * flux = speed * mitchell_n → Boyer ATP = flux*n/3.
         * Replaces bucketed EXPLORE_HIGH/LOW/STUCK with a smooth function. */
        eq->boyer = csos_derive_boyer_vitality(m->speed, m->mitchell_n);
        /* EXECUTE gets full credit (override to 1.0) */
        if (ph.decision == DECISION_EXECUTE) eq->boyer = 1.0;

        /* Vitality = geometric mean of all 5 contributions.
         * If ANY equation contributes 0, the organism is dead.
         * pow(g*f*m*mi*b, 1/5) */
        double product = eq->gouterman * eq->forster * eq->marcus
                       * eq->mitchell * eq->boyer;
        eq->vitality = product > 0 ? pow(product, 0.2) : 0;

        /* EMA smoothing: adaptive alpha = 1/sqrt(alive_cycles).
         * Fast at start, stabilizes over time — no hardcoded 0.1. */
        double v_alpha = csos_derive_vitality_alpha(eq->alive_cycles);
        eq->vitality_ema = eq->vitality_ema * (1.0 - v_alpha)
                         + eq->vitality * v_alpha;

        /* Track peak and alive cycles */
        if (eq->vitality > eq->vitality_peak)
            eq->vitality_peak = eq->vitality;
        if (eq->vitality > 0)
            eq->alive_cycles++;

        ph.vitality = eq->vitality;
    }

    /* ── CALVIN (pattern synthesis — adaptive frequency) ── */
    /* Rubisco-derived: rate ∝ [CO2]/Km. CO2 pressure drives frequency.
     * freq = 1 + (int)(atom_count * (1 - co2_pressure))
     * Full pool → every 1-2 cycles. Empty → every atom_count cycles.
     * Frequency scales with system complexity — no fixed MIN/MAX. */
    {
        double co2_pressure = (double)m->co2_count / (double)CSOS_CO2_POOL_SIZE;
        int freq_max = m->atom_count > 2 ? m->atom_count : 2;
        int calvin_freq = 2 + (int)((freq_max - 2) * (1.0 - co2_pressure));
        if (calvin_freq < 2) calvin_freq = 2;
        if (m->cycles > 0 && m->cycles % calvin_freq == 0) membrane_calvin(m);
    }

    /* ── FREE ENERGY: F = COMPLEXITY - ACCURACY (5 mechanisms) ── */
    /* Post-absorb: decompose F, assign hierarchy, detect rhythms,
     * discover causal atoms, counterfactual scoring, prune, active inference.
     * Returns next probe target (substrate hash). Stored in photon for LLM. */
    uint32_t probe = freeenergy_post_absorb(m, &ph);
    ph.probe_target = probe;
    m->probe_target = probe;

    /* ── dF/dt STRUCTURAL GUARANTEE ── */
    /* Streak threshold from atom complexity: log2(atoms) + 2.
     * More atoms = more inertia before triggering regime rebuild. */
    int streak_thresh = csos_derive_df_streak_threshold(m);
    if (m->dF_dt > 0) {
        m->dF_positive_streak++;
        if ((int)m->dF_positive_streak >= streak_thresh) {
            /* Regime change detected: force hierarchy rebuild */
            m->dF_positive_streak = 0;
            /* Widen resonance width to population ceiling (exploratory) */
            m->rw = csos_derive_rw_ceil(m);
        }
    } else {
        m->dF_positive_streak = 0;
    }

    /* ── Store in photon ring ── */
    m->ring[m->ring_head & (CSOS_PHOTON_RING - 1)] = ph;
    m->ring_head++;
    m->cycles++;

    return ph;
}

/* ═══ STATE PERSISTENCE ═══ */

/* Save membrane state to JSON file — buffered single-write.
 * Builds entire JSON in memory, then one fwrite() call.
 * Reduces 50+ fprintf syscalls to 1 write syscall. */
int csos_membrane_save(const csos_membrane_t *m, const char *dir) {
    char path[512];
    snprintf(path, sizeof(path), "%s/%s.mem.json", dir, m->name);

    /* Pre-allocate buffer (64KB covers even large motor arrays) */
    size_t bufsz = 65536;
    char *buf = (char *)malloc(bufsz);
    if (!buf) return -1;
    int pos = 0;

    /* Write aggregate state + motor memory */
    pos += snprintf(buf + pos, bufsz - pos, "{\n  \"name\":\"%s\",\n", m->name);
    pos += snprintf(buf + pos, bufsz - pos,
        "  \"gradient\":%.1f,\n  \"speed\":%.6f,\n  \"F\":%.6f,\n",
        m->gradient, m->speed, m->F);
    pos += snprintf(buf + pos, bufsz - pos,
        "  \"rw\":%.6f,\n  \"action_ratio\":%.6f,\n", m->rw, m->action_ratio);
    pos += snprintf(buf + pos, bufsz - pos,
        "  \"cycles\":%u,\n  \"signals\":%u,\n", m->cycles, m->signals);
    pos += snprintf(buf + pos, bufsz - pos,
        "  \"mitchell_n\":%d,\n  \"gradient_gap\":%.4f,\n",
        m->mitchell_n, m->gradient_gap);
    pos += snprintf(buf + pos, bufsz - pos,
        "  \"coupling_count\":%d,\n", m->coupling_count);
    pos += snprintf(buf + pos, bufsz - pos,
        "  \"mode\":%d,\n  \"decision\":%d,\n", m->mode, m->decision);
    pos += snprintf(buf + pos, bufsz - pos,
        "  \"consecutive_zero_delta\":%d,\n", m->consecutive_zero_delta);
    pos += snprintf(buf + pos, bufsz - pos,
        "  \"atom_count\":%d,\n", m->atom_count);

    /* Living equation state */
    pos += snprintf(buf + pos, bufsz - pos,
        "  \"equation\":{\"gouterman\":%.6f,\"forster\":%.6f,\"marcus\":%.6f,"
        "\"mitchell\":%.6f,\"boyer\":%.6f,\"vitality\":%.6f,"
        "\"vitality_ema\":%.6f,\"vitality_peak\":%.6f,\"alive_cycles\":%u},\n",
        m->equation.gouterman, m->equation.forster, m->equation.marcus,
        m->equation.mitchell, m->equation.boyer, m->equation.vitality,
        m->equation.vitality_ema, m->equation.vitality_peak, m->equation.alive_cycles);

    /* Dual-reactor state (ATP/NADPH coupling) */
    pos += snprintf(buf + pos, bufsz - pos,
        "  \"atp_pool\":%.6f,\n  \"nadph_pool\":%.6f,\n",
        m->atp_pool, m->nadph_pool);
    pos += snprintf(buf + pos, bufsz - pos,
        "  \"light_cycles\":%u,\n  \"dark_cycles\":%u,\n",
        m->light_cycles, m->dark_cycles);
    pos += snprintf(buf + pos, bufsz - pos,
        "  \"reactor_balance\":%.6f,\n  \"npq_dissipated\":%.6f,\n",
        m->reactor_balance, m->npq_dissipated);
    pos += snprintf(buf + pos, bufsz - pos,
        "  \"active_regime\":%d,\n  \"dF_positive_streak\":%u,\n",
        m->active_regime, m->dF_positive_streak);
    pos += snprintf(buf + pos, bufsz - pos,
        "  \"F_accuracy\":%.6f,\n  \"F_complexity\":%.6f,\n  \"F_free_energy\":%.6f,\n",
        m->F_accuracy, m->F_complexity, m->F_free_energy);

    /* Motor memory */
    pos += snprintf(buf + pos, bufsz - pos,
        "  \"motor_cycle\":%llu,\n  \"motor_count\":%d,\n",
        (unsigned long long)m->motor_cycle, m->motor_count);
    pos += snprintf(buf + pos, bufsz - pos, "  \"motor\":[\n");
    int motor_written = 0;
    for (int i = 0; i < CSOS_MAX_MOTOR && pos < (int)bufsz - 256; i++) {
        const csos_motor_t *e = &m->motor[i];
        if (e->substrate_hash == 0) continue;
        if (motor_written > 0)
            pos += snprintf(buf + pos, bufsz - pos, ",\n");
        pos += snprintf(buf + pos, bufsz - pos,
            "    {\"hash\":%u,\"last\":%llu,\"interval\":%llu,"
            "\"prev\":%llu,\"reps\":%u,\"strength\":%.6f}",
            e->substrate_hash, (unsigned long long)e->last_seen,
            (unsigned long long)e->interval, (unsigned long long)e->prev_interval,
            e->reps, e->strength);
        motor_written++;
    }
    pos += snprintf(buf + pos, bufsz - pos, "\n  ],\n");

    /* f_history (last 100 entries to keep file small) */
    int fstart = m->f_count > 100 ? m->f_count - 100 : 0;
    pos += snprintf(buf + pos, bufsz - pos, "  \"f_history\":[");
    for (int i = fstart; i < m->f_count && pos < (int)bufsz - 32; i++) {
        pos += snprintf(buf + pos, bufsz - pos,
            "%.4f%s", m->f_history[i], (i < m->f_count - 1) ? "," : "");
    }
    pos += snprintf(buf + pos, bufsz - pos, "],\n");

    /* Calvin atoms (names + params only, not full photon history) */
    pos += snprintf(buf + pos, bufsz - pos, "  \"calvin_atoms\":[\n");
    int first = 1;
    for (int i = 0; i < m->atom_count && pos < (int)bufsz - 256; i++) {
        if (strncmp(m->atoms[i].name, "calvin_", 7) != 0) continue;
        if (!first) pos += snprintf(buf + pos, bufsz - pos, ",\n");
        pos += snprintf(buf + pos, bufsz - pos,
            "    {\"name\":\"%s\",\"formula\":\"%s\",\"compute\":\"%s\",\"center\":%.4f}",
            m->atoms[i].name, m->atoms[i].formula, m->atoms[i].compute, m->atoms[i].params[0]);
        first = 0;
    }
    pos += snprintf(buf + pos, bufsz - pos, "\n  ]\n}\n");

    /* Single write syscall */
    FILE *f = fopen(path, "w");
    if (!f) { free(buf); return -1; }
    fwrite(buf, 1, pos, f);
    fclose(f);
    free(buf);
    return 0;
}

/* Load membrane state from JSON file */
int csos_membrane_load(csos_membrane_t *m, const char *dir) {
    char path[512];
    snprintf(path, sizeof(path), "%s/%s.mem.json", dir, m->name);
    FILE *f = fopen(path, "r");
    if (!f) return -1;

    char buf[32768] = {0};
    size_t n = fread(buf, 1, sizeof(buf) - 1, f);
    fclose(f);
    if (n == 0) return -1;
    buf[n] = 0;

    /* Simple extraction — we know the exact format we wrote */
    char tmp[256];
    /* Helper: find "key":value in buf */
    #define LOAD_DOUBLE(key, target) do { \
        char *p = strstr(buf, "\"" key "\":"); \
        if (p) { p += strlen("\"" key "\":"); target = strtod(p, NULL); } \
    } while(0)
    #define LOAD_INT(key, target) do { \
        char *p = strstr(buf, "\"" key "\":"); \
        if (p) { p += strlen("\"" key "\":"); target = (int)strtol(p, NULL, 10); } \
    } while(0)
    #define LOAD_UINT(key, target) do { \
        char *p = strstr(buf, "\"" key "\":"); \
        if (p) { p += strlen("\"" key "\":"); target = (uint32_t)strtoul(p, NULL, 10); } \
    } while(0)
    #define LOAD_U64(key, target) do { \
        char *p = strstr(buf, "\"" key "\":"); \
        if (p) { p += strlen("\"" key "\":"); target = (uint64_t)strtoull(p, NULL, 10); } \
    } while(0)

    LOAD_DOUBLE("gradient", m->gradient);
    LOAD_DOUBLE("speed", m->speed);
    LOAD_DOUBLE("F", m->F);
    LOAD_DOUBLE("rw", m->rw);
    LOAD_DOUBLE("action_ratio", m->action_ratio);
    LOAD_UINT("cycles", m->cycles);
    LOAD_UINT("signals", m->signals);
    LOAD_INT("mode", m->mode);
    LOAD_INT("decision", m->decision);
    LOAD_INT("consecutive_zero_delta", m->consecutive_zero_delta);
    LOAD_U64("motor_cycle", m->motor_cycle);

    /* Dual-reactor state */
    LOAD_DOUBLE("atp_pool", m->atp_pool);
    LOAD_DOUBLE("nadph_pool", m->nadph_pool);
    LOAD_UINT("light_cycles", m->light_cycles);
    LOAD_UINT("dark_cycles", m->dark_cycles);
    LOAD_DOUBLE("reactor_balance", m->reactor_balance);
    LOAD_DOUBLE("npq_dissipated", m->npq_dissipated);
    {   /* active_regime is uint8_t, needs special handling */
        int _ar = 0;
        LOAD_INT("active_regime", _ar);
        m->active_regime = (uint8_t)_ar;
    }
    LOAD_UINT("dF_positive_streak", m->dF_positive_streak);
    LOAD_DOUBLE("F_accuracy", m->F_accuracy);
    LOAD_DOUBLE("F_complexity", m->F_complexity);
    LOAD_DOUBLE("F_free_energy", m->F_free_energy);

    /* Parse motor memory array */
    char *mstart = strstr(buf, "\"motor\":[");
    if (mstart) {
        mstart += strlen("\"motor\":[");
        m->motor_count = 0;
        while (*mstart && *mstart != ']' && m->motor_count < CSOS_MAX_MOTOR) {
            char *entry = strstr(mstart, "{\"hash\":");
            if (!entry || entry > strchr(mstart, ']')) break;
            csos_motor_t *e = &m->motor[m->motor_count];
            memset(e, 0, sizeof(*e));
            char *p = entry + strlen("{\"hash\":");
            e->substrate_hash = (uint32_t)strtoul(p, &p, 10);
            char *lp = strstr(entry, "\"last\":"); if (lp) e->last_seen = strtoull(lp + 7, NULL, 10);
            char *ip = strstr(entry, "\"interval\":"); if (ip) e->interval = strtoull(ip + 11, NULL, 10);
            char *pp = strstr(entry, "\"prev\":"); if (pp) e->prev_interval = strtoull(pp + 7, NULL, 10);
            char *rp = strstr(entry, "\"reps\":"); if (rp) e->reps = (uint32_t)strtoul(rp + 7, NULL, 10);
            char *sp = strstr(entry, "\"strength\":"); if (sp) e->strength = strtod(sp + 11, NULL);
            m->motor_count++;
            mstart = strchr(entry, '}');
            if (mstart) mstart++; else break;
        }
    }

    /* Parse Calvin atoms — recreate them in the membrane */
    char *cstart = strstr(buf, "\"calvin_atoms\":[");
    if (cstart) {
        cstart += strlen("\"calvin_atoms\":[");
        while (*cstart && *cstart != ']' && m->atom_count < CSOS_MAX_ATOMS) {
            char *entry = strstr(cstart, "{\"name\":\"");
            if (!entry || entry > strchr(cstart, ']')) break;
            /* Extract name */
            char cname[CSOS_NAME_LEN] = {0}, cformula[CSOS_FORMULA_LEN] = {0};
            double center = 0;
            char *np = entry + strlen("{\"name\":\"");
            int ni = 0; while (*np && *np != '"' && ni < CSOS_NAME_LEN-1) cname[ni++] = *np++;
            char *fp = strstr(entry, "\"formula\":\"");
            if (fp) { fp += strlen("\"formula\":\""); int fi=0; while(*fp && *fp!='"' && fi<CSOS_FORMULA_LEN-1) cformula[fi++]=*fp++; }
            char *cp = strstr(entry, "\"center\":");
            if (cp) center = strtod(cp + strlen("\"center\":"), NULL);

            /* Check if this Calvin atom already exists (from equation init) */
            int exists = 0;
            for (int i = 0; i < m->atom_count; i++)
                if (strcmp(m->atoms[i].name, cname) == 0) { exists = 1; break; }

            if (!exists && cname[0]) {
                csos_atom_t *na = &m->atoms[m->atom_count];
                memset(na, 0, sizeof(csos_atom_t));
                strncpy(na->name, cname, CSOS_NAME_LEN - 1);
                strncpy(na->formula, cformula, CSOS_FORMULA_LEN - 1);
                snprintf(na->source, CSOS_NAME_LEN, "Calvin %s (restored)", m->name);
                strncpy(na->born_in, m->name, CSOS_NAME_LEN - 1);
                na->params[0] = center; na->param_count = 1;
                na->photon_cap = CSOS_ATOM_PHOTON_RING;
                na->photons = (csos_photon_t *)calloc(CSOS_ATOM_PHOTON_RING, sizeof(csos_photon_t));
                na->photon_head = 0;
                na->local_cap = CSOS_ATOM_PHOTON_RING;
                na->local_photons = (csos_photon_t *)calloc(CSOS_ATOM_PHOTON_RING, sizeof(csos_photon_t));
                na->local_head = 0;
                na->has_resonated = 0;
                na->last_resonated_value = 0;
                csos_atom_compute_rw(na);
                m->atom_count++;
            }
            cstart = strchr(entry, '}');
            if (cstart) cstart++; else break;
        }
    }

    /* Restore equation state */
    {
        char *eqp = strstr(buf, "\"equation\":{");
        if (eqp) {
            eqp += strlen("\"equation\":{");
            char *g = strstr(eqp, "\"gouterman\":"); if (g) m->equation.gouterman = strtod(g+13, NULL);
            char *f = strstr(eqp, "\"forster\":"); if (f) m->equation.forster = strtod(f+10, NULL);
            char *ma = strstr(eqp, "\"marcus\":"); if (ma) m->equation.marcus = strtod(ma+9, NULL);
            char *mi = strstr(eqp, "\"mitchell\":"); if (mi) m->equation.mitchell = strtod(mi+11, NULL);
            char *b = strstr(eqp, "\"boyer\":"); if (b) m->equation.boyer = strtod(b+8, NULL);
            char *v = strstr(eqp, "\"vitality\":"); if (v) m->equation.vitality = strtod(v+11, NULL);
            char *ve = strstr(eqp, "\"vitality_ema\":"); if (ve) m->equation.vitality_ema = strtod(ve+15, NULL);
            char *vp = strstr(eqp, "\"vitality_peak\":"); if (vp) m->equation.vitality_peak = strtod(vp+16, NULL);
            char *ac = strstr(eqp, "\"alive_cycles\":"); if (ac) m->equation.alive_cycles = (uint32_t)strtoul(ac+15, NULL, 10);
        }
    }

    /* Recompute dynamic RW from restored F value — population-derived bounds */
    double rw_fl = csos_derive_rw_floor(m);
    double rw_cl = csos_derive_rw_ceil(m);
    m->rw = rw_fl + (rw_cl - rw_fl) * m->F / (m->F + 1.0);

#ifdef CSOS_HAS_LLVM
    /* JIT compile all atoms including restored Calvin atoms.
     * Without this, restored Calvin atoms use slow formula_eval fallback. */
    if (m->atom_count > 0) csos_formula_jit_compile(m);
#endif

    #undef LOAD_DOUBLE
    #undef LOAD_INT
    #undef LOAD_UINT
    #undef LOAD_U64

    (void)tmp;
    return 0;
}

/* Save all membranes in organism */
int csos_organism_save(csos_organism_t *org) {
    char dir[512];
    snprintf(dir, sizeof(dir), "%s/.csos/rings", org->root);
    mkdir(dir, 0755);
    int saved = 0;
    for (int i = 0; i < org->count; i++) {
        if (org->membranes[i] && csos_membrane_save(org->membranes[i], dir) == 0)
            saved++;
    }
    return saved;
}

/* Load persisted state into organism's membranes */
int csos_organism_load(csos_organism_t *org) {
    char dir[512];
    snprintf(dir, sizeof(dir), "%s/.csos/rings", org->root);
    int loaded = 0;
    for (int i = 0; i < org->count; i++) {
        if (org->membranes[i] && csos_membrane_load(org->membranes[i], dir) == 0)
            loaded++;
    }
    return loaded;
}

/* ═══ COUPLING TENSOR (Forster strength between membranes) ═══ */

int csos_membrane_couple(csos_membrane_t *a, csos_membrane_t *b) {
    /* Compute bidirectional coupling strength from gradient ratio */
    double ga = a->gradient > 0 ? a->gradient : 1;
    double gb = b->gradient > 0 ? b->gradient : 1;
    double r = fabs(ga - gb) / (ga + gb);  /* Normalized distance */
    if (r < 0.001) r = 0.001;
    double coupling = pow(1.0 / r, 6.0);   /* Forster (1/r)^6 */
    if (coupling > 1e6) coupling = 1e6;     /* Cap for numerical stability */

    /* Record coupling on both membranes */
    if (a->coupling_count < CSOS_MAX_RINGS) {
        strncpy(a->couplings[a->coupling_count].peer_name, b->name, CSOS_NAME_LEN-1);
        a->couplings[a->coupling_count].coupling = coupling;
        /* local coupling */
        a->coupling_count++;
    }
    if (b->coupling_count < CSOS_MAX_RINGS) {
        strncpy(b->couplings[b->coupling_count].peer_name, a->name, CSOS_NAME_LEN-1);
        b->couplings[b->coupling_count].coupling = coupling;
        /* local coupling */
        b->coupling_count++;
    }
    return (coupling > 1.0) ? 1 : 0; /* 1 = coupled, 0 = too far */
}

double csos_membrane_coupling_strength(const csos_membrane_t *m, const char *peer) {
    for (int i = 0; i < m->coupling_count; i++)
        if (strcmp(m->couplings[i].peer_name, peer) == 0) return m->couplings[i].coupling;
    return 0.0;
}

/* ═══ ORGANISM (3-ring cascade) ═══ */

int csos_organism_init(csos_organism_t *org, const char *root) {
    memset(org, 0, sizeof(*org));
    strncpy(org->root, root, sizeof(org->root) - 1);
    /* Create ecosystem membranes */
    const char *names[] = {"eco_domain", "eco_cockpit", "eco_organism"};
    for (int i = 0; i < 3; i++) {
        org->membranes[i] = csos_membrane_create(names[i]);
        org->count++;
    }
    /* Load persisted state from previous runs */
    int loaded = csos_organism_load(org);
    if (loaded > 0) {
        fprintf(stderr, "[csos] Restored state for %d membranes from disk\n", loaded);
    }

    /* Load seed bank from previous cycles */
    {
        char sess_dir[512];
        snprintf(sess_dir, sizeof(sess_dir), "%s/.csos/sessions", root);
        int seeds_loaded = csos_seed_load(org, sess_dir);
        if (seeds_loaded > 0) {
            fprintf(stderr, "[csos] Restored %d seeds\n", seeds_loaded);
        }
    }

    /* Initial coupling tensor: domain→cockpit→organism cascade.
     * Forster (R₀/r)^6 — computed from gradient ratio at init time.
     * Coupling updates dynamically on each organism_absorb. */
    if (org->count >= 3) {
        csos_membrane_couple(org->membranes[0], org->membranes[1]); /* domain↔cockpit */
        csos_membrane_couple(org->membranes[1], org->membranes[2]); /* cockpit↔organism */
        csos_membrane_couple(org->membranes[0], org->membranes[2]); /* domain↔organism */
    }

    return 0;
}

void csos_organism_destroy(csos_organism_t *org) {
    /* Save seed bank */
    {
        char sess_dir[512];
        snprintf(sess_dir, sizeof(sess_dir), "%s/.csos/sessions", org->root);
        csos_seed_save(org, sess_dir);
    }
    /* Auto-save state before shutdown */
    int saved = csos_organism_save(org);
    if (saved > 0) {
        fprintf(stderr, "[csos] Saved state for %d membranes to disk\n", saved);
    }
    for (int i = 0; i < org->count; i++) {
        if (org->membranes[i]) {
            membrane_free_atoms(org->membranes[i]);
            free(org->membranes[i]);
        }
    }
}

csos_membrane_t *csos_organism_grow(csos_organism_t *org, const char *name) {
    csos_membrane_t *existing = csos_organism_find(org, name);
    if (existing) return existing;
    if (org->count >= CSOS_MAX_RINGS) return NULL;
    csos_membrane_t *m = csos_membrane_create(name);
    org->membranes[org->count++] = m;
    return m;
}

csos_membrane_t *csos_organism_find(csos_organism_t *org, const char *name) {
    for (int i = 0; i < org->count; i++)
        if (org->membranes[i] && strcmp(org->membranes[i]->name, name) == 0)
            return org->membranes[i];
    return NULL;
}

/* Number extraction from raw text */
static int extract_nums(const char *raw, double *out, int max) {
    int n = 0;
    const char *p = raw;
    while (*p && n < max) {
        while (*p && !(*p >= '0' && *p <= '9') && *p != '-' && *p != '+' && *p != '.') p++;
        if (!*p) break;
        char *end;
        double v = strtod(p, &end);
        if (end > p && (end - p) < 15) { out[n++] = v; p = end; }
        else p++;
    }
    return n;
}

/* Substrate hash */
static uint32_t sub_hash(const char *name) {
    uint32_t h = 0;
    for (const char *p = name; *p; p++) h = h * 31 + (uint8_t)*p;
    return 1000 + (h % 9000);
}

/* 3-ring membrane cascade (replaces gateway_absorb + 3x fly) */
csos_photon_t csos_organism_absorb(csos_organism_t *org, const char *substrate,
                                    const char *raw, uint8_t protocol) {
    double nums[20];
    int n = extract_nums(raw, nums, 20);
    uint32_t sh = sub_hash(substrate);

    csos_membrane_t *d = csos_organism_find(org, "eco_domain");
    csos_membrane_t *k = csos_organism_find(org, "eco_cockpit");
    csos_membrane_t *o = csos_organism_find(org, "eco_organism");

    double g0 = d ? d->gradient : 0;

    /* Domain: absorb raw signals */
    csos_photon_t last = {0};
    csos_membrane_absorb(d, (double)sh, sh, protocol);
    for (int i = 0; i < n; i++)
        last = csos_membrane_absorb(d, nums[i], sh, protocol);

    /* Cockpit: absorb ONE composite domain signal with Forster coupling.
     * k_ET = (1/τ)(R₀/r)^n — coupling = (source_rw / target_rw)^FORSTER_EXPONENT.
     * Composite signal = gradient + speed + F combined into single absorb.
     * Reduces 3 absorb calls → 1 (same physics, 3x fewer atom scans). */
    if (k && d) {
        double coupling_dk = pow(d->rw / (k->rw > 1e-10 ? k->rw : 1e-10),
                                 6  /* Förster 1948: k_ET ∝ (R₀/r)^6 — the actual exponent from the equation */);
        if (coupling_dk > 1.0) coupling_dk = 1.0;
        /* Composite: gradient carries the Mitchell signal, speed and F modulate it */
        double composite_dk = (d->gradient + d->speed + d->F) * coupling_dk;
        csos_membrane_absorb(k, composite_dk, sh, PROTO_INTERNAL);
    }

    /* Organism: absorb ONE composite cross-ring signal.
     * Same Forster derivation, combining domain + cockpit into single absorb. */
    if (o && d && k) {
        double coupling_do = pow(d->rw / (o->rw > 1e-10 ? o->rw : 1e-10),
                                 6  /* Förster 1948: k_ET ∝ (R₀/r)^6 — the actual exponent from the equation */);
        double coupling_ko = pow(k->rw / (o->rw > 1e-10 ? o->rw : 1e-10),
                                 6  /* Förster 1948: k_ET ∝ (R₀/r)^6 — the actual exponent from the equation */);
        if (coupling_do > 1.0) coupling_do = 1.0;
        if (coupling_ko > 1.0) coupling_ko = 1.0;
        double composite_o = d->gradient * coupling_do + k->gradient * coupling_ko;
        last = csos_membrane_absorb(o, composite_o, sh, PROTO_INTERNAL);
    }

    /* Gradient gap tracking: Forster coupling modulation.
     * gradient_gap = upstream - self. Positive = upstream has more evidence.
     * Used by coupling tensor to scale energy transfer rate.
     * Mitchell: ΔG = -n·F·Δψ — gradient_gap IS the Δψ between rings. */
    if (d && k) k->gradient_gap = d->gradient - k->gradient;
    if (k && o) o->gradient_gap = k->gradient - o->gradient;
    if (d && o) d->gradient_gap = o->gradient - d->gradient; /* Feedback loop */

    /* Periodic Forster diffuse + coupling tensor refresh */
    if (o && o->cycles > 0 && o->cycles % 10 == 0) {
        csos_membrane_diffuse(o, d);
        /* Refresh coupling tensor with current gradient ratios */
        if (d && k) {
            d->coupling_count = 0; k->coupling_count = 0;
            if (o) o->coupling_count = 0;
            csos_membrane_couple(d, k);
            csos_membrane_couple(k, o);
            csos_membrane_couple(d, o);
        }
    }

    /* Populate final photon with cascade results */
    last.delta = (int32_t)(d ? d->gradient - g0 : 0);
    last.decision = o ? o->decision : DECISION_EXPLORE;
    last.motor_strength = o ? csos_motor_strength(o, sh) : 0;

    return last;
}

/* ═══ FORSTER COUPLING (diffuse atoms between membranes) ═══ */

int csos_membrane_diffuse(csos_membrane_t *src, csos_membrane_t *tgt) {
    int transferred = 0;
    for (int i = 0; i < src->atom_count; i++) {
        int exists = 0;
        for (int j = 0; j < tgt->atom_count; j++)
            if (strcmp(src->atoms[i].name, tgt->atoms[j].name) == 0) { exists = 1; break; }
        if (exists) continue;
        double g = atom_gradient(&src->atoms[i]);
        if (g <= 0) continue;
        double r = 1.0 / (1.0 + g);
        if (pow(1.0 / (r > 1e-10 ? r : 1e-10), 6.0) > 1.0 && tgt->atom_count < CSOS_MAX_ATOMS) {
            csos_atom_t *na = &tgt->atoms[tgt->atom_count];
            memcpy(na, &src->atoms[i], sizeof(csos_atom_t));
            /* Deep copy photon arrays */
            na->photon_cap = CSOS_ATOM_PHOTON_RING;
            na->photons = (csos_photon_t *)calloc(CSOS_ATOM_PHOTON_RING, sizeof(csos_photon_t));
            memcpy(na->photons, src->atoms[i].photons, CSOS_ATOM_PHOTON_RING * sizeof(csos_photon_t));
            na->photon_head = src->atoms[i].photon_head;
            na->photon_count = src->atoms[i].photon_count;
            na->local_cap = CSOS_ATOM_PHOTON_RING;
            na->local_photons = (csos_photon_t *)calloc(CSOS_ATOM_PHOTON_RING, sizeof(csos_photon_t));
            na->local_head = 0;
            na->local_count = 0;
            na->has_resonated = src->atoms[i].has_resonated;
            na->last_resonated_value = src->atoms[i].last_resonated_value;
            char buf[CSOS_NAME_LEN];
            snprintf(buf, CSOS_NAME_LEN, "%s [%s]", src->atoms[i].source, src->name);
            strncpy(na->source, buf, CSOS_NAME_LEN - 1);
            strncpy(na->born_in, src->name, CSOS_NAME_LEN - 1);
            tgt->atom_count++;
            transferred++;
        }
    }
    return transferred;
}

/* ═══ OBSERVATION (see) ═══ */

int csos_membrane_see(const csos_membrane_t *m, const char *detail,
                      char *json, size_t sz) {
    if (!detail || strcmp(detail, "minimal") == 0) {
        snprintf(json, sz, "{\"name\":\"%s\",\"F\":%.4f,\"gradient\":%.0f,\"speed\":%.4f}",
                 m->name, m->F, m->gradient, m->speed);
    } else if (strcmp(detail, "standard") == 0) {
        snprintf(json, sz,
            "{\"name\":\"%s\",\"F\":%.4f,\"gradient\":%.0f,\"speed\":%.4f,"
            "\"cycles\":%u,\"atoms\":%d,\"resonance_width\":%.3f}",
            m->name, m->F, m->gradient, m->speed, m->cycles, m->atom_count, m->rw);
    } else if (strcmp(detail, "cockpit") == 0) {
        int total_ph = 0, res_ph = 0, calvin_n = 0;
        for (int i = 0; i < m->atom_count; i++) {
            total_ph += m->atoms[i].photon_count;
            res_ph += (int)atom_gradient(&m->atoms[i]);
            if (strncmp(m->atoms[i].name, "calvin_", 7) == 0) calvin_n++;
        }
        double spec = (double)calvin_n / (m->cycles > 0 ? m->cycles : 1);
        double ar = (double)res_ph / (total_ph > 0 ? total_ph : 1);
        double cr = (double)calvin_n / (m->atom_count > 0 ? m->atom_count : 1);
        int crossings = 0;
        for (int i = 1; i < m->f_count; i++) {
            int pa = m->f_history[i-1] > m->rw;
            int ca = m->f_history[i] > m->rw;
            if (pa != ca) crossings++;
        }
        snprintf(json, sz,
            "{\"name\":\"%s\",\"F\":%.4f,\"gradient\":%.0f,\"speed\":%.4f,"
            "\"resonance_width\":%.3f,\"specificity_delta\":%.4f,"
            "\"action_ratio\":%.4f,\"gradient_gap\":%.2f,"
            "\"calvin_rate\":%.4f,\"boundary_crossings\":%d,"
            "\"mitchell_n\":%d,"
            "\"mode\":\"%s\",\"decision\":\"%s\","
            "\"motor_entries\":%d,\"motor_cycle\":%llu}",
            m->name, m->F, m->gradient, m->speed, m->rw,
            spec, ar, m->gradient_gap, cr, crossings,
            m->mitchell_n,
            m->mode == MODE_PLAN ? "plan" : "build",
            (const char*[]){"EXPLORE","EXECUTE","ASK","STORE"}[m->decision],
            m->motor_count, (unsigned long long)m->motor_cycle);
    } else {
        /* full — include muscle memory top 5 */
        uint32_t top_h[5]; double top_s[5];
        int top_n = csos_motor_top(m, top_h, top_s, 5);
        int pos = snprintf(json, sz,
            "{\"name\":\"%s\",\"F\":%.4f,\"gradient\":%.0f,\"speed\":%.4f,"
            "\"cycles\":%u,\"atoms\":%d,\"motor\":[",
            m->name, m->F, m->gradient, m->speed, m->cycles, m->atom_count);
        for (int i = 0; i < top_n && pos < (int)sz - 80; i++) {
            if (i > 0) pos += snprintf(json + pos, sz - pos, ",");
            pos += snprintf(json + pos, sz - pos, "{\"hash\":%u,\"strength\":%.3f}",
                            top_h[i], top_s[i]);
        }
        snprintf(json + pos, sz - pos, "]}");
    }
    return 0;
}

/* ═══ HEALTH CHECK ═══ */

int csos_membrane_lint(const csos_membrane_t *m, char *json, size_t sz) {
    int issues = 0;
    if (m->atom_count == 0) issues++;
    if (m->F < 0 || m->F > 100) issues++;
    if (m->speed < 0) issues++;
    snprintf(json, sz, "{\"ring\":\"%s\",\"status\":\"%s\",\"compliance\":%.3f}",
             m->name, issues == 0 ? "pass" : "fail", 1.0 - issues * 0.25);
    return 0;
}

/* ═══ GREENHOUSE: PERSISTENCE (seed bank only) ═══ */

int csos_seed_save(const csos_organism_t *org, const char *dir) {
    char path[512];
    snprintf(path, sizeof(path), "%s/seeds.json", dir);
    FILE *f = fopen(path, "w");
    if (!f) return -1;
    fprintf(f, "{\"seeds\":[\n");
    for (int i = 0; i < org->seed_count; i++) {
        const csos_seed_t *s = &org->seeds[i];
        fprintf(f, "%s  {\"name\":\"%s\",\"formula\":\"%s\",\"compute\":\"%s\","
                "\"source\":\"%s\",\"center\":%.6f,\"strength\":%.6f,"
                "\"hash\":%u,\"cycle\":%u}",
                i > 0 ? ",\n" : "", s->name, s->formula, s->compute,
                s->source, s->center, s->strength,
                s->substrate_hash, s->harvest_cycle);
    }
    fprintf(f, "\n]}\n");
    fclose(f);
    return 0;
}

int csos_seed_load(csos_organism_t *org, const char *dir) {
    char path[512];
    snprintf(path, sizeof(path), "%s/seeds.json", dir);
    FILE *f = fopen(path, "r");
    if (!f) return 0;
    char buf[16384] = {0};
    size_t n = fread(buf, 1, sizeof(buf) - 1, f);
    fclose(f);
    if (n == 0) return 0;

    int loaded = 0;
    char *p = strstr(buf, "\"seeds\"");
    if (!p) return 0;
    p = strchr(p, '[');
    if (!p) return 0;

    while ((p = strstr(p, "{\"name\":\"")) != NULL && org->seed_count < CSOS_MAX_SEEDS) {
        csos_seed_t *s = &org->seeds[org->seed_count];
        memset(s, 0, sizeof(*s));
        char *ns = p + 9; int ni = 0;
        while (*ns && *ns != '"' && ni < CSOS_NAME_LEN - 1) s->name[ni++] = *ns++;
        char *fs = strstr(p, "\"formula\":\"");
        if (fs) { fs += 11; ni = 0; while (*fs && *fs != '"' && ni < CSOS_FORMULA_LEN - 1) s->formula[ni++] = *fs++; }
        char *cs = strstr(p, "\"compute\":\"");
        if (cs) { cs += 11; ni = 0; while (*cs && *cs != '"' && ni < CSOS_FORMULA_LEN - 1) s->compute[ni++] = *cs++; }
        char *ss = strstr(p, "\"source\":\"");
        if (ss) { ss += 10; ni = 0; while (*ss && *ss != '"' && ni < CSOS_NAME_LEN - 1) s->source[ni++] = *ss++; }
        char *ce = strstr(p, "\"center\":");
        if (ce) s->center = strtod(ce + 9, NULL);
        char *st = strstr(p, "\"strength\":");
        if (st) s->strength = strtod(st + 11, NULL);
        char *ha = strstr(p, "\"hash\":");
        if (ha) s->substrate_hash = (uint32_t)strtoul(ha + 7, NULL, 10);
        char *cy = strstr(p, "\"cycle\":");
        if (cy) s->harvest_cycle = (uint32_t)strtoul(cy + 8, NULL, 10);
        org->seed_count++;
        loaded++;
        p++;
    }
    return loaded;
}

/* ═══ THE LIVING EQUATION — EQUATE ═══
 *
 * This is THE paradigm shift: a single JSON view that shows the living equation
 * as a unified organism where all 5 equations contribute to vitality.
 *
 * The gap between the living world and the conceptual world is closed here:
 * each equation has BOTH its physics meaning AND its living meaning exposed.
 */

int csos_membrane_equate(const csos_membrane_t *mem, char *json, size_t sz) {
    const csos_equation_t *eq = &mem->equation;
    const char *dec[] = {"EXPLORE", "EXECUTE", "ASK", "STORE"};

    return snprintf(json, sz,
        "{\"membrane\":\"%s\","
        "\"vitality\":%.6f,"
        "\"vitality_ema\":%.6f,"
        "\"vitality_peak\":%.6f,"
        "\"alive_cycles\":%u,"
        "\"equations\":{"
          "\"gouterman\":{\"value\":%.6f,\"physics\":\"dE=hc/λ\",\"meaning\":\"signal resonance\",\"living\":\"what do I absorb?\"},"
          "\"forster\":{\"value\":%.6f,\"physics\":\"k=(1/τ)(R₀/r)⁶\",\"meaning\":\"coupling strength\",\"living\":\"how connected am I?\"},"
          "\"marcus\":{\"value\":%.6f,\"physics\":\"k=exp(-(ΔG+λ)²/4λkT)\",\"meaning\":\"prediction accuracy\",\"living\":\"how well do I learn?\"},"
          "\"mitchell\":{\"value\":%.6f,\"physics\":\"ΔG=-nFΔψ\",\"meaning\":\"life force rate\",\"living\":\"how much energy do I have?\"},"
          "\"boyer\":{\"value\":%.6f,\"physics\":\"ATP=flux·n/3\",\"meaning\":\"decision clarity\",\"living\":\"am I ready to act?\"}"
        "},"
        "\"state\":{"
          "\"gradient\":%.1f,\"speed\":%.6f,\"F\":%.6f,\"rw\":%.6f,"
          "\"decision\":\"%s\",\"mode\":\"%s\","
          "\"cycles\":%u,\"atoms\":%d,\"motors\":%d,"
          "\"calvin_atoms\":%d,\"co2_pool\":%d"
        "},"
        "\"reactor\":{"
          "\"atp_pool\":%.2f,\"nadph_pool\":%.2f,"
          "\"atp_produced\":%.2f,\"atp_consumed\":%.2f,"
          "\"nadph_produced\":%.2f,\"nadph_consumed\":%.2f,"
          "\"balance\":%.4f,"
          "\"light_cycles\":%u,\"dark_cycles\":%u,"
          "\"npq_active\":%d,\"npq_dissipated\":%.2f,"
          "\"F_complexity\":%.6f,\"F_accuracy\":%.6f,"
          "\"F_free_energy\":%.6f,\"dF_dt\":%.6f"
        "}}",
        mem->name,
        eq->vitality, eq->vitality_ema, eq->vitality_peak, eq->alive_cycles,
        eq->gouterman, eq->forster, eq->marcus, eq->mitchell, eq->boyer,
        mem->gradient, mem->speed, mem->F, mem->rw,
        dec[mem->decision & 3],
        mem->mode == MODE_BUILD ? "build" : "plan",
        mem->cycles, mem->atom_count, mem->motor_count,
        /* Count Calvin atoms */
        ({int ca=0; for(int i=0;i<mem->atom_count;i++) if(strncmp(mem->atoms[i].name,"calvin_",7)==0) ca++; ca;}),
        mem->co2_count,
        /* V14: Reactor state */
        mem->atp_pool, mem->nadph_pool,
        mem->atp_produced_this_cycle, mem->atp_consumed_this_cycle,
        mem->nadph_produced_this_cycle, mem->nadph_consumed_this_cycle,
        mem->reactor_balance,
        mem->light_cycles, mem->dark_cycles,
        (int)mem->npq_active, mem->npq_dissipated,
        mem->F_complexity, mem->F_accuracy,
        mem->F_free_energy, mem->dF_dt
    );
}

int csos_organism_equate(const csos_organism_t *org, char *json, size_t sz) {
    int pos = 0;
    pos += snprintf(json + pos, sz - pos, "{\"living_equation\":{\"rings\":[");

    for (int i = 0; i < org->count; i++) {
        if (!org->membranes[i]) continue;
        if (i > 0) pos += snprintf(json + pos, sz - pos, ",");
        char ring_json[4096] = {0};
        csos_membrane_equate(org->membranes[i], ring_json, sizeof(ring_json));
        pos += snprintf(json + pos, sz - pos, "%s", ring_json);
    }

    /* Organism-level vitality: geometric mean across all rings */
    double org_vitality = 0;
    if (org->count > 0) {
        double product = 1.0;
        int alive = 0;
        for (int i = 0; i < org->count; i++) {
            if (org->membranes[i] && org->membranes[i]->equation.vitality > 0) {
                product *= org->membranes[i]->equation.vitality;
                alive++;
            }
        }
        if (alive > 0) org_vitality = pow(product, 1.0 / alive);
    }

    /* Flow: the equation cascade showing signal path */
    pos += snprintf(json + pos, sz - pos,
        "],\"organism_vitality\":%.6f,"
        "\"flow\":{\"path\":\"signal → Gouterman(λ) → Marcus(ΔG) → Mitchell(nFΔψ) → Boyer(ATP) → Calvin(seed)\","
        "\"description\":\"Each signal flows through all 5 equations. Vitality = how well they work together.\"},"
        "\"bridge\":{\"living\":\"The organism absorbs signals (sunlight), resonates with what it knows (chlorophyll), "
        "corrects its errors (repair), accumulates evidence (proton gradient), decides when ready (ATP synthase), "
        "and synthesizes new patterns (carbon fixation).\","
        "\"conceptual\":\"The system matches inputs (Gouterman), transfers knowledge (Forster), "
        "corrects predictions (Marcus), accumulates confidence (Mitchell), gates decisions (Boyer), "
        "and discovers patterns (Calvin).\"},"
        "\"seeds\":%d"
        "}}",
        org_vitality, org->seed_count);

    return 0;
}
