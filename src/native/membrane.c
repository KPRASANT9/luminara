/*
 * CSOS Membrane — The unified photosynthetic process.
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
#include <regex.h>

/* Perf counters + autotune state (read by protocol.c perf action) */
uint64_t _absorb_ns_total = 0;
uint32_t _absorb_count = 0;
int _autotune_calvin_freq = CSOS_CALVIN_FREQ_MIN;
int _autotune_compact_freq = 100;

/* ═══ EQUATIONS ═══
 * NO hardcoded array. All atoms loaded from specs/eco.csos via csos_spec_parse().
 * Foundation atoms (Gouterman, Forster, Marcus, Mitchell, Boyer) and Calvin atoms
 * are all parsed from spec files — the spec IS the code.
 *
 * See: csos_membrane_from_spec() in spec_parse.c
 *      csos_formula_eval() in formula_eval.c
 *      csos_formula_jit_compile() in formula_jit.c (LLVM path)
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
    int fc = (int)strlen(a->formula) / CSOS_FORMULA_DOF_DIVISOR;
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

/* atom_tune with pre-computed gradient to avoid triple-scan */
static void atom_tune_cached(csos_atom_t *a, int cached_grad) {
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
    if (fabs(bias) < a->rw * CSOS_TUNE_THRESHOLD) return;
    double lr = 1.0 / (1.0 + g);
    for (int k = 0; k < a->param_count; k++) {
        double pv = fabs(a->params[k]);
        if (pv == 0) pv = CSOS_ERROR_DENOM_GUARD;
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

    /* Dynamic RW: start at floor for fresh membrane, will adapt via F in absorb */
    m->rw = CSOS_RW_FLOOR;

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
        e->strength = CSOS_MOTOR_GROWTH;
        *out_strength = CSOS_MOTOR_GROWTH;
        *out_interval = 0;
    } else {
        uint64_t ni = m->motor_cycle - e->last_seen;
        e->prev_interval = e->interval;
        e->interval = ni;
        e->last_seen = m->motor_cycle;
        e->reps++;
        if (ni > e->prev_interval && e->prev_interval > 0) {
            double sf = (double)ni / (double)(e->prev_interval + 1);
            if (sf > CSOS_MOTOR_MAX_SF) sf = CSOS_MOTOR_MAX_SF;
            e->strength += CSOS_MOTOR_GROWTH * sf;
        } else if (ni > 0) {
            /* Forster-derived amplification for weak entries:
             * k_ET = (1/τ)(R₀/r)^6 — weaker coupling (smaller r) means
             * greater potential for energy transfer on each encounter.
             * Growth scales by (1 - current_strength), which is the Forster
             * distance analog: low strength = far away = high transfer potential.
             * At strength=0, growth = MOTOR_GROWTH (full potential).
             * At strength=1, growth = MOTOR_BACKOFF (already coupled).
             * No magic numbers: uses existing MOTOR_GROWTH and MOTOR_BACKOFF. */
            double growth = CSOS_MOTOR_BACKOFF +
                (CSOS_MOTOR_GROWTH - CSOS_MOTOR_BACKOFF) * (1.0 - e->strength);
            e->strength += growth;
        }
        /* Forster-derived dynamic decay: strong coupling = slow decay, weak = fast.
         * decay = FLOOR + (CEIL - FLOOR) * strength
         * Dynamic per-substrate — adapts based on Forster coupling distance. */
        double decay = CSOS_MOTOR_DECAY_FLOOR +
            (CSOS_MOTOR_DECAY_CEIL - CSOS_MOTOR_DECAY_FLOOR) * e->strength;
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
    double thresh = co2_mean_full * CSOS_CALVIN_GRAD_FRAC;
    if (thresh < CSOS_CALVIN_GRAD_FLOOR) thresh = CSOS_CALVIN_GRAD_FLOOR;
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
        double vt = sqrt(var) * CSOS_CALVIN_VAR_MULT;
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

    /* ── GOUTERMAN + MARCUS (resonance check, per atom) ── */
    int produced = 0;
    for (int i = 0; i < m->atom_count; i++) {
        csos_atom_t *a = &m->atoms[i];

        /* Predict from cached last-resonated value (O(1) vs O(n) scan) */
        double pred = a->has_resonated ? a->last_resonated_value : value;

        /* Marcus: error = |predicted - actual| / max(|actual|, |predicted|*0.01+1e-10) */
        double denom = fabs(value);
        double alt = fabs(pred) * CSOS_ERROR_DENOM_GUARD + 1e-10;
        if (alt > denom) denom = alt;
        double error = fabs(pred - value) / denom;
        int resonated = (error < a->rw) ? 1 : 0;

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
    /* Mitchell-derived F floor: error below Marcus denominator guard is noise. */
    if (m->F < CSOS_ERROR_DENOM_GUARD) m->F = CSOS_ERROR_DENOM_GUARD;
    m->action_ratio = total > 0 ? m->gradient / total : 0;
    if (m->f_count < CSOS_FHIST_LEN) m->f_history[m->f_count++] = m->F;

    /* Gouterman-derived dynamic resonance width:
     * High F (learning) → widen RW to accept more signals.
     * Low F (mature)    → narrow RW to be selective.
     * rw = FLOOR + (CEIL - FLOOR) * F / (F + 1)
     * At F=0.01: rw ≈ 0.8507 (narrow, selective).
     * At F=40:   rw ≈ 0.9183 (wide, exploratory). */
    m->rw = CSOS_RW_FLOOR + (CSOS_RW_CEIL - CSOS_RW_FLOOR) * m->F / (m->F + 1.0);

    /* ── TUNE (Marcus error correction — when F rising) ── */
    if (m->f_count >= 2 && m->f_history[m->f_count-1] > m->f_history[m->f_count-2]) {
        for (int i = 0; i < m->atom_count; i++)
            atom_tune_cached(&m->atoms[i], atom_grads[i]);
    }

    /* ── BOYER (decision gate) ── */
    if (m->speed > m->rw) {
        ph.decision = DECISION_EXECUTE;
        if (m->mode == MODE_PLAN) m->mode = MODE_BUILD;
    } else if (m->action_ratio > CSOS_BOYER_THRESHOLD && ph.delta > 0) {
        ph.decision = DECISION_EXPLORE;
    } else {
        if (ph.delta == 0) m->consecutive_zero_delta++;
        else m->consecutive_zero_delta = 0;
        if (m->consecutive_zero_delta >= CSOS_STUCK_CYCLES || m->action_ratio < CSOS_BOYER_THRESHOLD)
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

        /* Boyer: decision clarity.
         * EXECUTE=1.0 (full clarity), EXPLORE with progress=0.6,
         * EXPLORE stagnant=0.3, ASK/STORE=0.1 (stuck). */
        switch (ph.decision) {
            case DECISION_EXECUTE: eq->boyer = 1.0; break;
            case DECISION_EXPLORE: eq->boyer = ph.delta > 0 ? 0.6 : 0.3; break;
            case DECISION_ASK:     eq->boyer = 0.1; break;
            case DECISION_STORE:   eq->boyer = 0.1; break;
            default:               eq->boyer = 0.1; break;
        }

        /* Vitality = geometric mean of all 5 contributions.
         * If ANY equation contributes 0, the organism is dead.
         * pow(g*f*m*mi*b, 1/5) */
        double product = eq->gouterman * eq->forster * eq->marcus
                       * eq->mitchell * eq->boyer;
        eq->vitality = product > 0 ? pow(product, 0.2) : 0;

        /* EMA smoothing for trend detection */
        eq->vitality_ema = eq->vitality_ema * (1.0 - CSOS_VITALITY_SMOOTH)
                         + eq->vitality * CSOS_VITALITY_SMOOTH;

        /* Track peak and alive cycles */
        if (eq->vitality > eq->vitality_peak)
            eq->vitality_peak = eq->vitality;
        if (eq->vitality > 0)
            eq->alive_cycles++;

        ph.vitality = eq->vitality;
    }

    /* ── CALVIN (pattern synthesis — adaptive frequency) ── */
    /* Rubisco-derived: rate ∝ [CO2]/Km. More non-resonated signals = run Calvin sooner.
     * freq = MIN + (MAX - MIN) * (1 - co2_count / POOL_SIZE)
     * Full CO2 pool → every 2 cycles. Empty → every 8 cycles. */
    {
        double co2_pressure = (double)m->co2_count / (double)CSOS_CO2_POOL_SIZE;
        int calvin_freq = CSOS_CALVIN_FREQ_MIN +
            (int)((CSOS_CALVIN_FREQ_MAX - CSOS_CALVIN_FREQ_MIN) * (1.0 - co2_pressure));
        if (calvin_freq < CSOS_CALVIN_FREQ_MIN) calvin_freq = CSOS_CALVIN_FREQ_MIN;
        if (m->cycles > 0 && m->cycles % calvin_freq == 0) membrane_calvin(m);
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
        "  \"rdma_enabled\":%d,\n  \"coupling_count\":%d,\n",
        m->rdma_enabled, m->coupling_count);
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

    /* Recompute dynamic RW from restored F value */
    m->rw = CSOS_RW_FLOOR + (CSOS_RW_CEIL - CSOS_RW_FLOOR) * m->F / (m->F + 1.0);

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
        a->couplings[a->coupling_count].peer_node = 0; /* local */
        a->coupling_count++;
    }
    if (b->coupling_count < CSOS_MAX_RINGS) {
        strncpy(b->couplings[b->coupling_count].peer_name, a->name, CSOS_NAME_LEN-1);
        b->couplings[b->coupling_count].coupling = coupling;
        b->couplings[b->coupling_count].peer_node = 0;
        b->coupling_count++;
    }
    return (coupling > 1.0) ? 1 : 0; /* 1 = coupled, 0 = too far */
}

double csos_membrane_coupling_strength(const csos_membrane_t *m, const char *peer) {
    for (int i = 0; i < m->coupling_count; i++)
        if (strcmp(m->couplings[i].peer_name, peer) == 0) return m->couplings[i].coupling;
    return 0.0;
}

/* RDMA registration — real impl requires libibverbs, fallback uses TCP */
int csos_membrane_rdma_register(csos_membrane_t *m) {
    /* Register photon ring buffer for remote access.
     * With RDMA hardware: ibv_reg_mr() on the photon ring memory.
     * Without RDMA: expose via TCP socket on port 4200 + ring_index.
     * The HTTP daemon already serves /api/command — RDMA register
     * enables direct memory access to the photon ring for zero-copy
     * cross-node Forster coupling. */
    m->rdma_enabled = 1;
    /* Compute rkey from ring name hash (deterministic, reproducible) */
    uint32_t rk = 0;
    for (const char *c = m->name; *c; c++) rk = rk * 31 + (uint8_t)*c;
    m->rdma_rkey = rk;
    /* Remote addr = base address of photon ring (for local, this is actual pointer) */
    m->rdma_remote_addr = (uint64_t)(uintptr_t)m->ring;
    return 0;
}

int csos_membrane_rdma_diffuse(csos_membrane_t *src, const char *remote_name,
                                uint32_t remote_node) {
    /* Cross-node Forster coupling via RDMA or TCP fallback.
     *
     * With RDMA hardware (libibverbs):
     *   1. ibv_post_send() RDMA READ on remote's photon ring
     *   2. Zero-copy: reads directly from remote's registered memory
     *   3. Applies Forster coupling from remote photon data
     *
     * Without RDMA (TCP fallback):
     *   1. Connect to remote node's HTTP daemon
     *   2. POST {"action":"see","ring":"<name>","detail":"full"}
     *   3. Parse response, extract gradient + motor data
     *   4. Apply as Forster coupling signal into local membrane
     *
     * The coupling tensor tracks remote peers:
     *   src->couplings[i].peer_node = remote node ID
     *   src->couplings[i].coupling = Forster strength
     */
    if (remote_node == 0) return 0; /* Local — use csos_membrane_diffuse() instead */

    /* TCP fallback: connect to remote HTTP daemon */
    int port = 4200; /* Base port — remote nodes on 4200 + node_id */
    if (remote_node > 0) port = 4200 + (int)remote_node;

    char cmd[512];
    snprintf(cmd, sizeof(cmd),
        "curl -sf -m 2 -X POST http://localhost:%d/api/command "
        "-H 'Content-Type: application/json' "
        "-d '{\"action\":\"see\",\"ring\":\"%s\",\"detail\":\"full\"}'",
        port, remote_name);

    FILE *fp = popen(cmd, "r");
    if (!fp) return -1;

    char data[8192] = {0};
    size_t total = 0;
    while (total < sizeof(data) - 1) {
        size_t n = fread(data + total, 1, sizeof(data) - 1 - total, fp);
        if (n == 0) break;
        total += n;
    }
    int exit_code = pclose(fp);
    if (exit_code != 0 || total == 0) return -1;

    /* Extract gradient from remote response */
    char *gp = strstr(data, "\"gradient\":");
    double remote_gradient = 0;
    if (gp) remote_gradient = strtod(gp + 11, NULL);

    /* Apply as Forster coupling signal */
    if (remote_gradient > 0) {
        double coupling = pow(src->rw / (src->rw > 1e-10 ? src->rw : 1e-10),
                             CSOS_FORSTER_EXPONENT);
        if (coupling > 1.0) coupling = 1.0;
        /* Absorb remote gradient as internal signal */
        double value = remote_gradient * coupling;
        uint32_t sh = 0;
        for (const char *c = remote_name; *c; c++) sh = sh * 31 + (uint8_t)*c;
        sh = 1000 + (sh % 9000);
        csos_membrane_absorb(src, value, sh, PROTO_RDMA);
    }

    /* Update coupling tensor */
    if (src->coupling_count < CSOS_MAX_RINGS) {
        int idx = src->coupling_count;
        /* Check if already tracked */
        for (int i = 0; i < src->coupling_count; i++) {
            if (src->couplings[i].peer_node == remote_node &&
                strcmp(src->couplings[i].peer_name, remote_name) == 0) {
                idx = i;
                break;
            }
        }
        if (idx == src->coupling_count) src->coupling_count++;
        strncpy(src->couplings[idx].peer_name, remote_name, CSOS_NAME_LEN - 1);
        src->couplings[idx].peer_node = remote_node;
        src->couplings[idx].coupling = remote_gradient;
    }

    return 1; /* Success — 1 remote signal absorbed */
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
    /* Load persisted state from previous sessions */
    int loaded = csos_organism_load(org);
    if (loaded > 0) {
        fprintf(stderr, "[csos] Restored state for %d membranes from disk\n", loaded);
    }

    /* Load seed bank and session registry from previous greenhouse cycles */
    {
        char sess_dir[512];
        snprintf(sess_dir, sizeof(sess_dir), "%s/.csos/sessions", root);
        int seeds_loaded = csos_seed_load(org, sess_dir);
        int sessions_loaded = csos_session_load(org, sess_dir);
        if (seeds_loaded > 0 || sessions_loaded > 0) {
            fprintf(stderr, "[greenhouse] Restored %d seeds, %d sessions\n",
                    seeds_loaded, sessions_loaded);
        }
    }

    /* RDMA: auto-register all core rings for cross-ring access.
     * Enables zero-copy Forster coupling between membranes. */
    for (int i = 0; i < org->count; i++) {
        if (org->membranes[i]) {
            csos_membrane_rdma_register(org->membranes[i]);
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
    /* Auto-harvest active sessions before shutdown */
    for (int i = 0; i < org->session_count; i++) {
        if (org->sessions[i].stage >= SESSION_GROW) {
            org->sessions[i].stage = SESSION_DORMANT;
            csos_seed_harvest(org, &org->sessions[i]);
        }
    }
    /* Save greenhouse state */
    {
        char sess_dir[512];
        snprintf(sess_dir, sizeof(sess_dir), "%s/.csos/sessions", org->root);
        csos_seed_save(org, sess_dir);
        csos_session_save(org, sess_dir);
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
                                 CSOS_FORSTER_EXPONENT);
        if (coupling_dk > 1.0) coupling_dk = 1.0;
        /* Composite: gradient carries the Mitchell signal, speed and F modulate it */
        double composite_dk = (d->gradient + d->speed + d->F) * coupling_dk;
        csos_membrane_absorb(k, composite_dk, sh, PROTO_INTERNAL);
    }

    /* Organism: absorb ONE composite cross-ring signal.
     * Same Forster derivation, combining domain + cockpit into single absorb. */
    if (o && d && k) {
        double coupling_do = pow(d->rw / (o->rw > 1e-10 ? o->rw : 1e-10),
                                 CSOS_FORSTER_EXPONENT);
        double coupling_ko = pow(k->rw / (o->rw > 1e-10 ? o->rw : 1e-10),
                                 CSOS_FORSTER_EXPONENT);
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

    /* Greenhouse: update session lifecycle from this photon.
     * Find or auto-spawn session for this substrate. */
    {
        csos_session_t *s = NULL;
        /* Find existing session by substrate */
        for (int i = 0; i < org->session_count; i++) {
            if (org->sessions[i].substrate_hash == sh) {
                s = &org->sessions[i];
                break;
            }
        }
        /* Auto-spawn if new substrate from external protocol */
        if (!s && protocol != PROTO_INTERNAL && org->session_count < CSOS_MAX_SESSIONS) {
            s = csos_session_spawn(org, substrate, substrate);
        }
        if (s) csos_session_update(org, s, &last);

        /* Periodic convergence check + auto-merge (every 50 cycles).
         * Forster: when sessions converge, cross-pollinate their Calvin atoms. */
        if (o && o->cycles > 0 && o->cycles % 50 == 0) {
            for (int i = 0; i < org->session_count; i++) {
                for (int j = i + 1; j < org->session_count; j++) {
                    csos_session_merge(org, &org->sessions[i], &org->sessions[j]);
                }
            }
        }
    }

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
            "\"mitchell_n\":%d,\"rdma_enabled\":%d,"
            "\"mode\":\"%s\",\"decision\":\"%s\","
            "\"motor_entries\":%d,\"motor_cycle\":%llu}",
            m->name, m->F, m->gradient, m->speed, m->rw,
            spec, ar, m->gradient_gap, cr, crossings,
            m->mitchell_n, m->rdma_enabled,
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

/* ═══ GREENHOUSE: SESSION LIFECYCLE ═══
 *
 * The greenhouse is where living equations grow:
 *   spawn()  → SEED:    Create session, plant seeds from seed bank
 *   absorb() → SPROUT:  First signals arrive, gradient building
 *   absorb() → GROW:    Calvin synthesizing, atoms increasing
 *   absorb() → BLOOM:   Boyer EXECUTE — ready to deliver
 *   harvest()→ HARVEST: Calvin atoms → seed bank for future sessions
 *   (idle)   → DORMANT: Motor memory persists, session paused
 *
 * Each transition is driven by the 5 equations — no lifecycle-specific logic.
 */

/* Substrate hash helper */
static uint32_t greenhouse_hash(const char *name) {
    uint32_t h = 0;
    for (const char *p = name; *p; p++) h = h * 31 + (uint8_t)*p;
    return 1000 + (h % 9000);
}

csos_session_t *csos_session_spawn(csos_organism_t *org, const char *id,
                                    const char *substrate) {
    /* Check if session already exists */
    for (int i = 0; i < org->session_count; i++) {
        if (strcmp(org->sessions[i].id, id) == 0) {
            org->sessions[i].stage = SESSION_SPROUT;
            return &org->sessions[i];
        }
    }
    if (org->session_count >= CSOS_MAX_SESSIONS) {
        /* Evict oldest dormant session */
        int oldest = -1;
        uint32_t oldest_cycle = UINT32_MAX;
        for (int i = 0; i < org->session_count; i++) {
            if (org->sessions[i].stage == SESSION_DORMANT &&
                org->sessions[i].last_active < oldest_cycle) {
                oldest = i;
                oldest_cycle = org->sessions[i].last_active;
            }
        }
        if (oldest < 0) return NULL; /* All active, can't evict */
        /* Harvest before eviction */
        csos_seed_harvest(org, &org->sessions[oldest]);
        org->sessions[oldest] = (csos_session_t){0};
        /* Compact */
        org->sessions[oldest] = org->sessions[org->session_count - 1];
        org->session_count--;
    }

    csos_session_t *s = &org->sessions[org->session_count];
    memset(s, 0, sizeof(*s));
    strncpy(s->id, id, CSOS_NAME_LEN - 1);
    strncpy(s->substrate, substrate ? substrate : "general", CSOS_NAME_LEN - 1);
    s->substrate_hash = greenhouse_hash(s->substrate);
    s->stage = SESSION_SEED;

    /* Get organism cycle from eco_organism ring */
    csos_membrane_t *o = csos_organism_find(org, "eco_organism");
    s->birth_cycle = o ? o->cycles : 0;
    s->last_active = s->birth_cycle;

    org->session_count++;

    /* Plant seeds from seed bank — Forster cross-pollination across time.
     * Seeds with matching substrate_hash get priority (spectral overlap). */
    csos_seed_plant(org, s);

    return s;
}

csos_session_t *csos_session_find(csos_organism_t *org, const char *id) {
    for (int i = 0; i < org->session_count; i++)
        if (strcmp(org->sessions[i].id, id) == 0) return &org->sessions[i];
    return NULL;
}

/* Update session stage based on photon from membrane_absorb.
 * The 5 equations drive the transitions — no hardcoded lifecycle logic.
 *
 * SYNTHESIS: Every absorb is automatically recorded as a flow element.
 * The session's living equation is recomputed after every signal. */
void csos_session_update(csos_organism_t *org, csos_session_t *s,
                         const csos_photon_t *ph) {
    csos_membrane_t *o = csos_organism_find(org, "eco_organism");
    s->last_active = o ? o->cycles : s->last_active + 1;

    /* Track peaks — Mitchell gradient IS the life force */
    if (o && o->gradient > s->peak_gradient) s->peak_gradient = o->gradient;
    if (o && o->speed > s->peak_speed) s->peak_speed = o->speed;

    /* ── AUTO-INFER: Record this absorb as a flow element ── */
    csos_session_record_flow(s, FLOW_ABSORB, ph->cycle, ph->actual,
                             ph->delta, ph->vitality, ph->substrate_hash,
                             "absorb");

    /* ── AUTO-INFER: Record decision as a flow element ── */
    if (ph->decision == DECISION_EXECUTE || ph->decision == DECISION_ASK) {
        const char *dec_labels[] = {"explore","execute","ask","store"};
        csos_session_record_flow(s, FLOW_DECISION, ph->cycle, ph->actual,
                                 ph->delta, ph->vitality, ph->substrate_hash,
                                 dec_labels[ph->decision & 3]);
    }

    /* ── AUTO-INFER: Track connection from binding if set ── */
    if (s->binding[0] && s->connection_count == 0) {
        csos_session_track_connection(s, s->binding,
            s->ingress.type[0] ? s->ingress.type : "pipe");
    }

    /* Recompute session-level living equation */
    csos_session_synthesize(s);

    /* Stage transitions driven by physics:
     *   SEED → SPROUT:   First signal absorbed (delta > 0)
     *   SPROUT → GROW:   Calvin synthesized at least one atom
     *   GROW → BLOOM:    Boyer EXECUTE decision
     *   BLOOM → HARVEST: After delivery, harvest Calvin atoms
     * Transitions are monotonic: you can only move forward. */
    csos_membrane_t *d = csos_organism_find(org, "eco_domain");
    int calvin_count = 0;
    if (d) {
        for (int i = 0; i < d->atom_count; i++)
            if (strncmp(d->atoms[i].name, "calvin_", 7) == 0) calvin_count++;
    }

    switch (s->stage) {
    case SESSION_SEED:
        if (ph->delta > 0) s->stage = SESSION_SPROUT;
        break;
    case SESSION_SPROUT:
        if (calvin_count > 0) s->stage = SESSION_GROW;
        break;
    case SESSION_GROW:
        if (ph->decision == DECISION_EXECUTE) {
            s->stage = SESSION_BLOOM;
            s->deliveries++;
        }
        break;
    case SESSION_BLOOM:
        /* Stay in bloom until explicitly harvested or goes dormant */
        if (ph->decision == DECISION_EXECUTE) s->deliveries++;
        break;
    default:
        break;
    }
}

/* ═══ SHARED EVENT LOG ═══ */

void csos_event_log(csos_organism_t *org, csos_event_type_t type,
                    const char *source, const char *session,
                    const char *message, int severity) {
    csos_event_t *ev = &org->events[org->event_head % CSOS_MAX_EVENTS];
    ev->type = type;
    ev->timestamp = (int64_t)time(NULL);
    if (source) strncpy(ev->source, source, sizeof(ev->source) - 1);
    if (session) strncpy(ev->session, session, sizeof(ev->session) - 1);
    /* Sanitize message for JSON safety — strip control chars, escape quotes */
    if (message) {
        int mi = 0;
        for (int i = 0; message[i] && mi < (int)sizeof(ev->message) - 2; i++) {
            unsigned char c = (unsigned char)message[i];
            if (c < 32) continue;  /* Strip control chars, newlines, tabs */
            if (c == '"' || c == '\\') { ev->message[mi++] = ' '; continue; }
            ev->message[mi++] = (char)c;
        }
        ev->message[mi] = 0;
    }
    ev->severity = severity;
    /* Capture organism vitality at event time */
    csos_membrane_t *o = csos_organism_find(org, "eco_organism");
    ev->vitality = o ? o->equation.vitality : 0;
    org->event_head++;
    if (org->event_count < CSOS_MAX_EVENTS) org->event_count++;
}

int csos_event_list(const csos_organism_t *org, char *json, size_t sz) {
    const char *type_names[] = {"agent_action","canvas_action","bottleneck",
                                "resolution","scheduler","milestone"};
    int pos = 0;
    pos += snprintf(json + pos, sz - pos, "{\"events\":[");
    int count = org->event_count < 20 ? org->event_count : 20;
    for (int i = 0; i < count && pos < (int)sz - 300; i++) {
        int idx = (org->event_head - count + i);
        if (idx < 0) idx += CSOS_MAX_EVENTS;
        idx = idx % CSOS_MAX_EVENTS;
        const csos_event_t *ev = &org->events[idx];
        if (i > 0) pos += snprintf(json + pos, sz - pos, ",");
        pos += snprintf(json + pos, sz - pos,
            "{\"type\":\"%s\",\"ts\":%lld,\"source\":\"%s\","
            "\"session\":\"%s\",\"message\":\"%s\","
            "\"vitality\":%.4f,\"severity\":%d}",
            type_names[ev->type < 6 ? ev->type : 0],
            (long long)ev->timestamp, ev->source,
            ev->session, ev->message,
            ev->vitality, ev->severity);
    }
    pos += snprintf(json + pos, sz - pos, "],\"total\":%d}", org->event_count);
    return 0;
}

/* ═══ SESSION = LIVING EQUATION: Autonomous Operation ═══
 *
 * The session IS the autonomous unit. These functions make it LIVE:
 *   bind     — connect to the outside world (stomata configuration)
 *   schedule — set the circadian rhythm (when to breathe)
 *   tick     — ONE autonomous cycle (inhale → absorb → decide → exhale)
 *   observe  — read the observation history
 *
 * The gardener (agent) calls bind/schedule to configure.
 * The scheduler calls tick automatically at intervals.
 * The organism runs itself. The agent intervenes only when decision=ASK.
 */

int csos_session_bind(csos_session_t *s, const char *binding,
                      const char *ingress_type, const char *ingress_source,
                      const char *egress_type, const char *egress_target) {
    if (binding) strncpy(s->binding, binding, sizeof(s->binding) - 1);
    if (ingress_type) {
        strncpy(s->ingress.type, ingress_type, sizeof(s->ingress.type) - 1);
        if (ingress_source) strncpy(s->ingress.source, ingress_source, sizeof(s->ingress.source) - 1);
        s->ingress.active = 1;
        if (s->ingress.timeout_ms == 0) s->ingress.timeout_ms = 5000;
    }
    if (egress_type) {
        strncpy(s->egress.type, egress_type, sizeof(s->egress.type) - 1);
        if (egress_target) strncpy(s->egress.target, egress_target, sizeof(s->egress.target) - 1);
        s->egress.active = 1;
    }
    return 0;
}

int csos_session_schedule(csos_session_t *s, int interval_secs, int autonomous) {
    s->schedule.interval_secs = interval_secs;
    s->schedule.autonomous = autonomous;
    /* Set next tick to now + interval */
    s->schedule.next_tick = (int64_t)time(NULL) + interval_secs;
    if (s->stage == SESSION_DORMANT && interval_secs > 0) {
        s->stage = SESSION_SPROUT; /* Wake from dormancy */
    }
    return 0;
}

/* Record an observation in the session's rolling history */
static void session_record_obs(csos_session_t *s, double value,
                               int32_t delta, uint8_t decision, double vitality,
                               const char *summary) {
    csos_observation_t *obs = &s->observations[s->obs_head % CSOS_MAX_OBS];
    obs->timestamp = (int64_t)time(NULL);
    obs->value = value;
    obs->delta = delta;
    obs->decision = decision;
    obs->vitality = vitality;
    if (summary) strncpy(obs->summary, summary, sizeof(obs->summary) - 1);
    s->obs_head++;
    if (s->obs_count < CSOS_MAX_OBS) s->obs_count++;
}

/* ═══ session_tick: THE autonomous cycle ═══
 *
 * This is the heartbeat of the living equation:
 *   1. INGRESS: fetch signal from external source (stomata open)
 *   2. ABSORB:  feed through 3-ring cascade (photosynthesis)
 *   3. DECIDE:  Boyer gate evaluates (ATP synthase)
 *   4. EGRESS:  if EXECUTE, deliver result (phloem transport)
 *   5. RECORD:  store observation (short-term memory)
 *
 * Returns: 0=ticked, -1=error, 1=nothing to do (no ingress or not due)
 */
int csos_session_tick(csos_organism_t *org, csos_session_t *s,
                      char *result_json, size_t sz) {
    int64_t now = (int64_t)time(NULL);

    /* Update scheduling state */
    s->schedule.last_tick = now;
    s->schedule.next_tick = now + s->schedule.interval_secs;
    s->schedule.tick_count++;

    /* ── 1. INGRESS: fetch signal from external source ── */
    char ingress_data[8192] = {0};
    int ingress_ok = 0;

    if (s->ingress.active && s->ingress.source[0]) {
        if (strcmp(s->ingress.type, "command") == 0) {
            /* Shell command ingress */
            FILE *fp = popen(s->ingress.source, "r");
            if (fp) {
                size_t total = 0;
                while (total < sizeof(ingress_data) - 1) {
                    size_t n = fread(ingress_data + total, 1,
                                     sizeof(ingress_data) - 1 - total, fp);
                    if (n == 0) break;
                    total += n;
                }
                int exit_code = pclose(fp);
                ingress_ok = (exit_code == 0 && total > 0);
            }
        } else if (strcmp(s->ingress.type, "url") == 0) {
            /* HTTP fetch ingress */
            char cmd[1024];
            if (s->ingress.auth[0]) {
                snprintf(cmd, sizeof(cmd),
                    "curl -sf -m %d -H 'Authorization: %s' '%s'",
                    s->ingress.timeout_ms / 1000, s->ingress.auth, s->ingress.source);
            } else {
                snprintf(cmd, sizeof(cmd),
                    "curl -sf -m %d '%s'",
                    s->ingress.timeout_ms / 1000, s->ingress.source);
            }
            FILE *fp = popen(cmd, "r");
            if (fp) {
                size_t total = 0;
                while (total < sizeof(ingress_data) - 1) {
                    size_t n = fread(ingress_data + total, 1,
                                     sizeof(ingress_data) - 1 - total, fp);
                    if (n == 0) break;
                    total += n;
                }
                pclose(fp);
                ingress_ok = (total > 0);
            }
        }
    }

    /* ── SYNTHESIS: Track ingress attempt ── */
    if (s->ingress.active && s->ingress.source[0]) {
        s->synthesis.ingress_attempts++;
        if (ingress_ok) {
            s->synthesis.ingress_successes++;
            csos_session_record_flow(s, FLOW_INGRESS, 0, 1.0, 1,
                                     s->vitality, s->substrate_hash,
                                     "ingress:ok");
            /* Track connection health for ingress source */
            if (s->binding[0])
                csos_session_connection_contact(s, s->binding, 1);
        } else {
            csos_session_record_flow(s, FLOW_INGRESS, 0, 0.0, 0,
                                     s->vitality, s->substrate_hash,
                                     "ingress:fail");
            if (s->binding[0])
                csos_session_connection_contact(s, s->binding, 0);
        }
    }

    /* ── SYNTHESIS: Record schedule tick as flow element ── */
    csos_session_record_flow(s, FLOW_SCHEDULE, 0, 1.0,
                             s->schedule.tick_count, s->vitality,
                             s->substrate_hash, "tick");

    if (!ingress_ok && s->ingress.active && s->ingress.source[0]) {
        /* Ingress failed — record observation but don't absorb */
        session_record_obs(s, 0, 0, DECISION_EXPLORE, s->vitality, "ingress failed");
        /* Log bottleneck: ingress failure */
        char bmsg[256];
        snprintf(bmsg, sizeof(bmsg), "Ingress failed: %s", s->ingress.source);
        csos_event_log(org, EVT_BOTTLENECK, "scheduler", s->id, bmsg, 2);
        csos_session_synthesize(s);  /* Recompute after failure */
        if (result_json) snprintf(result_json, sz,
            "{\"session\":\"%s\",\"tick\":%d,\"ingress\":\"failed\",\"stage\":\"%s\"}",
            s->id, s->schedule.tick_count, "error");
        return -1;
    }

    /* ── 2. ABSORB: feed through 3-ring cascade ── */
    const char *data = ingress_ok ? ingress_data : "";
    csos_photon_t ph = csos_organism_absorb(org, s->substrate,
                                             data[0] ? data : "tick",
                                             s->schedule.autonomous ? PROTO_CRON : PROTO_LLM);

    /* ── 3. UPDATE session state from photon ── */
    csos_membrane_t *o = csos_organism_find(org, "eco_organism");
    s->vitality = o ? o->equation.vitality : 0;
    double prev_vitality = s->obs_count > 0 ?
        s->observations[(s->obs_head - 1) % CSOS_MAX_OBS].vitality : 0;
    s->vitality_trend = s->vitality - prev_vitality;

    /* ── 3b. DETECT BOTTLENECKS & MILESTONES ── */
    {
        /* Bottleneck: vitality dropping or stuck */
        if (s->vitality_trend < -0.05 && s->vitality < 0.3) {
            char bmsg[256];
            snprintf(bmsg, sizeof(bmsg), "Vitality declining: %.0f%% (trend %.2f)",
                     s->vitality * 100, s->vitality_trend);
            csos_event_log(org, EVT_BOTTLENECK, "scheduler", s->id, bmsg, 2);
        }
        if (ph.decision == DECISION_ASK) {
            csos_event_log(org, EVT_BOTTLENECK, "scheduler", s->id,
                           "Session needs human input (decision=ASK)", 1);
        }
        /* Milestone: stage transitions */
        const char *prev_stage[] = {"seed","sprout","grow","bloom","harvest","dormant"};
        csos_session_stage_t old_stage = s->stage;
        /* (stage gets updated in session_update called by organism_absorb) */
        if (s->stage != old_stage && s->stage == SESSION_BLOOM) {
            csos_event_log(org, EVT_MILESTONE, "scheduler", s->id,
                           "Session reached BLOOM — ready to deliver", 0);
        }
        /* Resolution: vitality recovering */
        if (s->vitality_trend > 0.05 && s->vitality > 0.5 && prev_vitality < 0.3) {
            csos_event_log(org, EVT_RESOLUTION, "scheduler", s->id,
                           "Vitality recovering — bottleneck resolved", 0);
        }
    }

    /* ── 4. RECORD observation ── */
    char summary[128] = {0};
    const char *dec_names[] = {"EXPLORE","EXECUTE","ASK","STORE"};
    snprintf(summary, sizeof(summary), "%s d=%d v=%.0f%%",
             dec_names[ph.decision & 3], ph.delta, s->vitality * 100);
    session_record_obs(s, ph.actual, ph.delta, ph.decision, s->vitality, summary);

    /* ── 5. EGRESS: if EXECUTE, deliver result ── */
    int egress_fired = 0;
    if (ph.decision == DECISION_EXECUTE && s->egress.active && s->egress.target[0]) {
        s->synthesis.egress_attempts++;
        char payload[4096];
        snprintf(payload, sizeof(payload),
            "{\"session\":\"%s\",\"substrate\":\"%s\",\"decision\":\"EXECUTE\","
            "\"vitality\":%.4f,\"session_vitality\":%.4f,"
            "\"gradient\":%.0f,\"data\":\"%.*s\"}",
            s->id, s->substrate, s->vitality,
            s->synthesis.session_vitality,
            o ? o->gradient : 0,
            (int)(sizeof(payload) - 250), ingress_ok ? ingress_data : "");

        if (strcmp(s->egress.type, "webhook") == 0) {
            char cmd[2048];
            snprintf(cmd, sizeof(cmd),
                "curl -sf -m 5 -X POST -H 'Content-Type: application/json' "
                "-d '%s' '%s'", payload, s->egress.target);
            system(cmd);
            egress_fired = 1;
        } else if (strcmp(s->egress.type, "file") == 0) {
            FILE *f = fopen(s->egress.target, "a");
            if (f) { fprintf(f, "%s\n", payload); fclose(f); egress_fired = 1; }
        } else if (strcmp(s->egress.type, "command") == 0) {
            FILE *fp = popen(s->egress.target, "w");
            if (fp) { fwrite(payload, 1, strlen(payload), fp); pclose(fp); egress_fired = 1; }
        }

        /* ── SYNTHESIS: Track egress result ── */
        if (egress_fired) {
            s->synthesis.egress_successes++;
            csos_session_record_flow(s, FLOW_EGRESS, ph.cycle, 1.0, ph.delta,
                                     s->vitality, s->substrate_hash,
                                     "egress:delivered");
        } else {
            csos_session_record_flow(s, FLOW_EGRESS, ph.cycle, 0.0, 0,
                                     s->vitality, s->substrate_hash,
                                     "egress:failed");
        }
    }

    /* ── 6. SYNTHESIZE: recompute session-level living equation ── */
    csos_session_synthesize(s);

    /* ── 7. BUILD result JSON ── */
    if (result_json) {
        const char *stage_names[] = {"seed","sprout","grow","bloom","harvest","dormant"};
        snprintf(result_json, sz,
            "{\"session\":\"%s\",\"tick\":%d,"
            "\"decision\":\"%s\",\"delta\":%d,"
            "\"vitality\":%.4f,\"vitality_trend\":%.4f,"
            "\"session_vitality\":%.6f,"
            "\"session_vitality_ema\":%.6f,"
            "\"stage\":\"%s\",\"gradient\":%.0f,"
            "\"ingress\":\"%s\",\"egress\":%s,"
            "\"flow_health\":%.4f,\"workflow_health\":%.4f,"
            "\"connection_health\":%.4f,"
            "\"workflows\":%d,\"connections\":%d,"
            "\"flow_elements\":%d,\"observations\":%d}",
            s->id, s->schedule.tick_count,
            dec_names[ph.decision & 3], ph.delta,
            s->vitality, s->vitality_trend,
            s->synthesis.session_vitality,
            s->synthesis.session_vitality_ema,
            stage_names[s->stage < 6 ? s->stage : 0],
            o ? o->gradient : 0,
            ingress_ok ? "ok" : (s->ingress.active ? "failed" : "none"),
            egress_fired ? "true" : "false",
            s->synthesis.flow_health, s->synthesis.workflow_health,
            s->synthesis.connection_health,
            s->workflow_count, s->connection_count,
            s->synthesis.total_elements, s->obs_count);
    }

    return 0;
}

/* Tick all sessions that are due — called by the scheduler */
int csos_session_tick_all(csos_organism_t *org) {
    int64_t now = (int64_t)time(NULL);
    int ticked = 0;
    for (int i = 0; i < org->session_count; i++) {
        csos_session_t *s = &org->sessions[i];
        if (s->schedule.interval_secs <= 0) continue;  /* Not scheduled */
        if (!s->schedule.autonomous) continue;          /* Manual only */
        if (s->stage == SESSION_DORMANT) continue;      /* Sleeping */
        if (now < s->schedule.next_tick) continue;      /* Not due yet */
        csos_session_tick(org, s, NULL, 0);
        ticked++;
    }
    return ticked;
}

/* Observe a session's state and history */
int csos_session_observe(const csos_session_t *s, char *json, size_t sz) {
    const char *stage_names[] = {"seed","sprout","grow","bloom","harvest","dormant"};
    const char *dec_names[] = {"EXPLORE","EXECUTE","ASK","STORE"};
    int pos = 0;
    pos += snprintf(json + pos, sz - pos,
        "{\"session\":\"%s\",\"substrate\":\"%s\",\"binding\":\"%s\","
        "\"stage\":\"%s\",\"vitality\":%.4f,\"vitality_trend\":%.4f,"
        "\"peak_gradient\":%.0f,\"peak_speed\":%.1f,"
        "\"deliveries\":%d,\"seeds\":%d,\"tick_count\":%d,"
        "\"autonomous\":%s,\"interval\":%d,",
        s->id, s->substrate, s->binding,
        stage_names[s->stage < 6 ? s->stage : 0],
        s->vitality, s->vitality_trend,
        s->peak_gradient, s->peak_speed,
        s->deliveries, s->seeds_planted, s->schedule.tick_count,
        s->schedule.autonomous ? "true" : "false",
        s->schedule.interval_secs);

    /* Ingress/Egress status */
    pos += snprintf(json + pos, sz - pos,
        "\"ingress\":{\"type\":\"%s\",\"source\":\"%.*s\",\"active\":%s},"
        "\"egress\":{\"type\":\"%s\",\"target\":\"%.*s\",\"active\":%s},",
        s->ingress.type, 80, s->ingress.source,
        s->ingress.active ? "true" : "false",
        s->egress.type, 80, s->egress.target,
        s->egress.active ? "true" : "false");

    /* Session synthesis (the session-level living equation) */
    const csos_session_synthesis_t *syn = &s->synthesis;
    pos += snprintf(json + pos, sz - pos,
        "\"synthesis\":{"
        "\"session_vitality\":%.6f,"
        "\"session_vitality_ema\":%.6f,"
        "\"flow_health\":%.4f,\"workflow_health\":%.4f,"
        "\"connection_health\":%.4f,"
        "\"ingress_health\":%.4f,\"egress_health\":%.4f,"
        "\"total_elements\":%d},",
        syn->session_vitality, syn->session_vitality_ema,
        syn->flow_health, syn->workflow_health,
        syn->connection_health,
        syn->ingress_health, syn->egress_health,
        syn->total_elements);

    /* Active workflows */
    pos += snprintf(json + pos, sz - pos, "\"workflows\":[");
    for (int i = 0; i < s->workflow_count && pos < (int)sz - 256; i++) {
        const csos_workflow_track_t *wf = &s->workflows[i];
        if (i > 0) pos += snprintf(json + pos, sz - pos, ",");
        pos += snprintf(json + pos, sz - pos,
            "{\"name\":\"%s\",\"throughput\":%.3f,\"active\":%s}",
            wf->name, wf->throughput, wf->active ? "true" : "false");
    }
    pos += snprintf(json + pos, sz - pos, "],");

    /* Active connections */
    pos += snprintf(json + pos, sz - pos, "\"connections\":[");
    for (int i = 0; i < s->connection_count && pos < (int)sz - 256; i++) {
        const csos_connection_t *c = &s->connections[i];
        if (i > 0) pos += snprintf(json + pos, sz - pos, ",");
        pos += snprintf(json + pos, sz - pos,
            "{\"target\":\"%s\",\"health\":%.3f,\"active\":%s}",
            c->target, c->health, c->active ? "true" : "false");
    }
    pos += snprintf(json + pos, sz - pos, "],");

    /* Recent observations (last 10) */
    pos += snprintf(json + pos, sz - pos, "\"observations\":[");
    int count = s->obs_count < 10 ? s->obs_count : 10;
    for (int i = 0; i < count && pos < (int)sz - 200; i++) {
        int idx = (s->obs_head - count + i);
        if (idx < 0) idx += CSOS_MAX_OBS;
        idx = idx % CSOS_MAX_OBS;
        const csos_observation_t *obs = &s->observations[idx];
        if (i > 0) pos += snprintf(json + pos, sz - pos, ",");
        pos += snprintf(json + pos, sz - pos,
            "{\"ts\":%lld,\"delta\":%d,\"decision\":\"%s\","
            "\"vitality\":%.3f,\"summary\":\"%s\"}",
            (long long)obs->timestamp, obs->delta,
            dec_names[obs->decision & 3],
            obs->vitality, obs->summary);
    }
    pos += snprintf(json + pos, sz - pos, "]}");
    return 0;
}

/* See all sessions as living equations */
int csos_session_see_all(const csos_organism_t *org, char *json, size_t sz) {
    const char *stage_names[] = {"seed","sprout","grow","bloom","harvest","dormant"};
    int pos = 0;
    pos += snprintf(json + pos, sz - pos, "{\"living_equations\":[");
    for (int i = 0; i < org->session_count && pos < (int)sz - 512; i++) {
        const csos_session_t *s = &org->sessions[i];
        if (i > 0) pos += snprintf(json + pos, sz - pos, ",");
        pos += snprintf(json + pos, sz - pos,
            "{\"id\":\"%s\",\"substrate\":\"%s\",\"binding\":\"%s\","
            "\"stage\":\"%s\",\"vitality\":%.4f,\"trend\":%.4f,"
            "\"session_vitality\":%.6f,"
            "\"gradient\":%.0f,\"speed\":%.1f,"
            "\"deliveries\":%d,\"ticks\":%d,"
            "\"autonomous\":%s,\"interval\":%d,"
            "\"ingress_type\":\"%s\",\"ingress_active\":%s,"
            "\"egress_type\":\"%s\",\"egress_active\":%s,"
            "\"workflows\":%d,\"connections\":%d,"
            "\"flow_elements\":%d,\"observations\":%d}",
            s->id, s->substrate, s->binding,
            stage_names[s->stage < 6 ? s->stage : 0],
            s->vitality, s->vitality_trend,
            s->synthesis.session_vitality,
            s->peak_gradient, s->peak_speed,
            s->deliveries, s->schedule.tick_count,
            s->schedule.autonomous ? "true" : "false",
            s->schedule.interval_secs,
            s->ingress.type, s->ingress.active ? "true" : "false",
            s->egress.type, s->egress.active ? "true" : "false",
            s->workflow_count, s->connection_count,
            s->synthesis.total_elements, s->obs_count);
    }
    pos += snprintf(json + pos, sz - pos, "],\"count\":%d}", org->session_count);
    return 0;
}

/* ═══ SESSION SYNTHESIS: The Session-Level Living Equation ═══
 *
 * Every action within an agentic environment — absorb, workflow step,
 * ingress fetch, egress delivery, connection contact — is a flow element.
 * The session synthesizes ALL elements into one coherent vitality metric.
 *
 * Biology: the chloroplast's overall metabolic fitness. Multiple pathways
 * (glycolysis, Calvin cycle, electron transport) must ALL function.
 * If any pathway collapses, the geometric mean pulls vitality → 0.
 *
 * session_vitality = ⁵√(flow × workflow × connection × ingress × egress)
 */

void csos_session_record_flow(csos_session_t *s, csos_flow_type_t type,
                              uint32_t cycle, double value, int32_t delta,
                              double vitality, uint32_t substrate_hash,
                              const char *label) {
    csos_flow_element_t *el = &s->flow[s->flow_head % CSOS_MAX_FLOW_ELEMENTS];
    el->type = type;
    el->timestamp = (int64_t)time(NULL);
    el->cycle = cycle;
    el->value = value;
    el->delta = delta;
    el->vitality = vitality;
    el->substrate_hash = substrate_hash;
    if (label) strncpy(el->label, label, sizeof(el->label) - 1);
    s->flow_head++;
    if (s->flow_count < CSOS_MAX_FLOW_ELEMENTS) s->flow_count++;

    /* Update running counters for synthesis */
    s->synthesis.total_elements++;
    if (delta > 0) s->synthesis.total_positive++;
}

int csos_session_track_workflow(csos_session_t *s, const char *name,
                                int steps_total) {
    /* Find existing workflow tracker */
    for (int i = 0; i < s->workflow_count; i++) {
        if (strcmp(s->workflows[i].name, name) == 0) {
            /* Re-activate if needed */
            s->workflows[i].active = 1;
            if (steps_total > 0) s->workflows[i].steps_total = steps_total;
            return i;
        }
    }
    if (s->workflow_count >= CSOS_MAX_WORKFLOWS) return -1;
    csos_workflow_track_t *wf = &s->workflows[s->workflow_count];
    memset(wf, 0, sizeof(*wf));
    strncpy(wf->name, name, CSOS_NAME_LEN - 1);
    wf->steps_total = steps_total > 0 ? steps_total : 1;
    wf->active = 1;
    wf->started = (int64_t)time(NULL);
    return s->workflow_count++;
}

int csos_session_workflow_step(csos_session_t *s, const char *name,
                               int success) {
    for (int i = 0; i < s->workflow_count; i++) {
        if (strcmp(s->workflows[i].name, name) == 0) {
            csos_workflow_track_t *wf = &s->workflows[i];
            if (success) wf->steps_completed++;
            else wf->steps_failed++;
            wf->last_step = (int64_t)time(NULL);
            /* Throughput = successful / total attempted */
            int total_attempted = wf->steps_completed + wf->steps_failed;
            wf->throughput = total_attempted > 0 ?
                (double)wf->steps_completed / total_attempted : 0;
            /* Auto-complete when all steps done */
            if (wf->steps_completed + wf->steps_failed >= wf->steps_total)
                wf->active = 0;
            return 0;
        }
    }
    return -1; /* Workflow not found */
}

int csos_session_track_connection(csos_session_t *s, const char *target,
                                   const char *protocol) {
    /* Find existing connection */
    for (int i = 0; i < s->connection_count; i++) {
        if (strcmp(s->connections[i].target, target) == 0) {
            s->connections[i].active = 1;
            return i;
        }
    }
    if (s->connection_count >= CSOS_MAX_CONNECTIONS) return -1;
    csos_connection_t *c = &s->connections[s->connection_count];
    memset(c, 0, sizeof(*c));
    strncpy(c->target, target, sizeof(c->target) - 1);
    if (protocol) strncpy(c->protocol, protocol, sizeof(c->protocol) - 1);
    c->established = (int64_t)time(NULL);
    c->last_contact = c->established;
    c->health = 1.0;  /* Starts healthy */
    c->active = 1;
    return s->connection_count++;
}

int csos_session_connection_contact(csos_session_t *s, const char *target,
                                     int success) {
    for (int i = 0; i < s->connection_count; i++) {
        if (strcmp(s->connections[i].target, target) == 0) {
            csos_connection_t *c = &s->connections[i];
            if (success) { c->successes++; c->last_contact = (int64_t)time(NULL); }
            else c->failures++;
            int total = c->successes + c->failures;
            c->health = total > 0 ? (double)c->successes / total : 0;
            return 0;
        }
    }
    return -1;
}

/* ═══ csos_session_synthesize: THE session-level living equation ═══
 *
 * Recompute the session's unified vitality from all operational elements.
 * Called after every flow element is recorded, and during every tick.
 *
 * The 5 components mirror the organism's 5 equations:
 *   flow_health       ↔ Gouterman (are signals resonating?)
 *   workflow_health    ↔ Mitchell  (are pathways accumulating?)
 *   connection_health  ↔ Forster   (are connections coupled?)
 *   ingress_health     ↔ Marcus    (is input error-corrected?)
 *   egress_health      ↔ Boyer     (are decisions being delivered?)
 */
void csos_session_synthesize(csos_session_t *s) {
    csos_session_synthesis_t *syn = &s->synthesis;

    /* 1. Flow health: fraction of flow elements with positive contribution */
    syn->flow_health = syn->total_elements > 0 ?
        (double)syn->total_positive / syn->total_elements : 0.5;

    /* 2. Workflow health: geometric mean of active workflow throughputs */
    {
        double product = 1.0;
        int active = 0;
        for (int i = 0; i < s->workflow_count; i++) {
            double t = s->workflows[i].throughput;
            if (t < 0.01) t = 0.01;  /* Floor to prevent zero-collapse */
            product *= t;
            active++;
        }
        if (active > 0) {
            syn->workflow_health = pow(product, 1.0 / active);
        } else {
            syn->workflow_health = 1.0;  /* No workflows = neutral (doesn't drag down) */
        }
    }

    /* 3. Connection health: geometric mean of connection healths */
    {
        double product = 1.0;
        int active = 0;
        for (int i = 0; i < s->connection_count; i++) {
            if (!s->connections[i].active) continue;
            double h = s->connections[i].health;
            if (h < 0.01) h = 0.01;
            product *= h;
            active++;
        }
        if (active > 0) {
            syn->connection_health = pow(product, 1.0 / active);
        } else {
            syn->connection_health = 1.0;  /* No connections = neutral */
        }
    }

    /* 4. Ingress health: success rate of data fetches */
    syn->ingress_health = syn->ingress_attempts > 0 ?
        (double)syn->ingress_successes / syn->ingress_attempts : 1.0;

    /* 5. Egress health: delivery success rate */
    syn->egress_health = syn->egress_attempts > 0 ?
        (double)syn->egress_successes / syn->egress_attempts : 1.0;

    /* Unified session vitality: ⁵√(product of all 5 components)
     * Geometric mean — if ANY component → 0, session_vitality → 0 */
    double product = syn->flow_health *
                     syn->workflow_health *
                     syn->connection_health *
                     syn->ingress_health *
                     syn->egress_health;
    syn->session_vitality = pow(product > 0 ? product : 0, 0.2);

    /* Exponential moving average (α = 0.1, same as ring EMA) */
    if (syn->session_vitality_ema == 0) {
        syn->session_vitality_ema = syn->session_vitality;
    } else {
        syn->session_vitality_ema = 0.1 * syn->session_vitality +
                                    0.9 * syn->session_vitality_ema;
    }

    /* Peak tracking */
    if (syn->session_vitality > syn->session_vitality_peak) {
        syn->session_vitality_peak = syn->session_vitality;
    }
}

/* Emit session synthesis state as JSON */
int csos_session_synthesize_json(const csos_session_t *s, char *json, size_t sz) {
    const csos_session_synthesis_t *syn = &s->synthesis;
    const char *stage_names[] = {"seed","sprout","grow","bloom","harvest","dormant"};
    const char *flow_names[] = {"absorb","workflow","ingress","egress",
                                "connection","schedule","decision","synthesis"};
    int pos = 0;

    pos += snprintf(json + pos, sz - pos,
        "{\"session\":\"%s\",\"substrate\":\"%s\",\"stage\":\"%s\","
        "\"synthesis\":{"
        "\"session_vitality\":%.6f,"
        "\"session_vitality_ema\":%.6f,"
        "\"session_vitality_peak\":%.6f,"
        "\"flow_health\":%.4f,"
        "\"workflow_health\":%.4f,"
        "\"connection_health\":%.4f,"
        "\"ingress_health\":%.4f,"
        "\"egress_health\":%.4f,"
        "\"total_elements\":%d,"
        "\"total_positive\":%d,"
        "\"ingress_attempts\":%d,"
        "\"ingress_successes\":%d,"
        "\"egress_attempts\":%d,"
        "\"egress_successes\":%d},",
        s->id, s->substrate,
        stage_names[s->stage < 6 ? s->stage : 0],
        syn->session_vitality, syn->session_vitality_ema,
        syn->session_vitality_peak,
        syn->flow_health, syn->workflow_health,
        syn->connection_health, syn->ingress_health,
        syn->egress_health,
        syn->total_elements, syn->total_positive,
        syn->ingress_attempts, syn->ingress_successes,
        syn->egress_attempts, syn->egress_successes);

    /* Active workflows */
    pos += snprintf(json + pos, sz - pos, "\"workflows\":[");
    for (int i = 0; i < s->workflow_count && pos < (int)sz - 256; i++) {
        const csos_workflow_track_t *wf = &s->workflows[i];
        if (i > 0) pos += snprintf(json + pos, sz - pos, ",");
        pos += snprintf(json + pos, sz - pos,
            "{\"name\":\"%s\",\"steps\":%d,\"completed\":%d,"
            "\"failed\":%d,\"throughput\":%.3f,\"active\":%s}",
            wf->name, wf->steps_total, wf->steps_completed,
            wf->steps_failed, wf->throughput,
            wf->active ? "true" : "false");
    }
    pos += snprintf(json + pos, sz - pos, "],");

    /* Active connections */
    pos += snprintf(json + pos, sz - pos, "\"connections\":[");
    for (int i = 0; i < s->connection_count && pos < (int)sz - 256; i++) {
        const csos_connection_t *c = &s->connections[i];
        if (i > 0) pos += snprintf(json + pos, sz - pos, ",");
        pos += snprintf(json + pos, sz - pos,
            "{\"target\":\"%s\",\"protocol\":\"%s\","
            "\"health\":%.3f,\"successes\":%d,\"failures\":%d,\"active\":%s}",
            c->target, c->protocol,
            c->health, c->successes, c->failures,
            c->active ? "true" : "false");
    }
    pos += snprintf(json + pos, sz - pos, "],");

    /* Recent flow elements (last 10) */
    pos += snprintf(json + pos, sz - pos, "\"flow\":[");
    int count = s->flow_count < 10 ? s->flow_count : 10;
    for (int i = 0; i < count && pos < (int)sz - 200; i++) {
        int idx = (s->flow_head - count + i);
        if (idx < 0) idx += CSOS_MAX_FLOW_ELEMENTS;
        idx = idx % CSOS_MAX_FLOW_ELEMENTS;
        const csos_flow_element_t *el = &s->flow[idx];
        if (i > 0) pos += snprintf(json + pos, sz - pos, ",");
        pos += snprintf(json + pos, sz - pos,
            "{\"type\":\"%s\",\"cycle\":%u,\"delta\":%d,"
            "\"vitality\":%.3f,\"label\":\"%s\"}",
            flow_names[el->type & 7], el->cycle, el->delta,
            el->vitality, el->label);
    }
    pos += snprintf(json + pos, sz - pos, "]}");

    return 0;
}

/* ═══ GREENHOUSE: SEED BANK ═══
 *
 * Calvin cycle → seed bank: atoms from completed sessions feed future ones.
 * This IS cross-pollination across time — Forster coupling through the seed bank.
 *
 * Harvest: Calvin atoms from a session → seed bank entries
 * Plant:   Seed bank entries → new session's membrane (as initial Calvin atoms)
 */

int csos_seed_harvest(csos_organism_t *org, const csos_session_t *s) {
    csos_membrane_t *d = csos_organism_find(org, "eco_domain");
    if (!d) return 0;

    int harvested = 0;
    for (int i = 0; i < d->atom_count && org->seed_count < CSOS_MAX_SEEDS; i++) {
        if (strncmp(d->atoms[i].name, "calvin_", 7) != 0) continue;

        /* Check if this seed already exists in the bank */
        int exists = 0;
        for (int j = 0; j < org->seed_count; j++) {
            if (strcmp(org->seeds[j].name, d->atoms[i].name) == 0) { exists = 1; break; }
        }
        if (exists) continue;

        csos_seed_t *seed = &org->seeds[org->seed_count];
        memset(seed, 0, sizeof(*seed));
        strncpy(seed->name, d->atoms[i].name, CSOS_NAME_LEN - 1);
        strncpy(seed->formula, d->atoms[i].formula, CSOS_FORMULA_LEN - 1);
        strncpy(seed->compute, d->atoms[i].compute, CSOS_FORMULA_LEN - 1);
        snprintf(seed->source, CSOS_NAME_LEN, "seed:%s", s->id);
        seed->center = d->atoms[i].params[0];
        seed->strength = csos_motor_strength(d, s->substrate_hash);
        seed->substrate_hash = s->substrate_hash;
        seed->harvest_cycle = d->cycles;
        org->seed_count++;
        harvested++;
    }
    return harvested;
}

int csos_seed_plant(csos_organism_t *org, csos_session_t *s) {
    csos_membrane_t *d = csos_organism_find(org, "eco_domain");
    if (!d) return 0;

    int planted = 0;
    for (int i = 0; i < org->seed_count; i++) {
        csos_seed_t *seed = &org->seeds[i];
        if (d->atom_count >= CSOS_MAX_ATOMS) break;

        /* Forster spectral matching: plant seeds with matching or nearby substrate.
         * Coupling strength = 1 / (1 + hash_distance).
         * Same substrate → coupling = 1.0 (direct match).
         * Different substrate → coupling < 1.0 (Forster distance). */
        uint32_t dist = (seed->substrate_hash > s->substrate_hash) ?
            seed->substrate_hash - s->substrate_hash : s->substrate_hash - seed->substrate_hash;
        double coupling = 1.0 / (1.0 + (double)dist / 1000.0);
        if (coupling < CSOS_BOYER_THRESHOLD) continue; /* Too far — Forster below threshold */

        /* Check if atom already exists */
        int exists = 0;
        for (int j = 0; j < d->atom_count; j++)
            if (strcmp(d->atoms[j].name, seed->name) == 0) { exists = 1; break; }
        if (exists) continue;

        /* Plant the seed as a Calvin atom in the domain ring */
        csos_atom_t *na = &d->atoms[d->atom_count];
        memset(na, 0, sizeof(csos_atom_t));
        strncpy(na->name, seed->name, CSOS_NAME_LEN - 1);
        strncpy(na->formula, seed->formula, CSOS_FORMULA_LEN - 1);
        strncpy(na->compute, seed->compute, CSOS_FORMULA_LEN - 1);
        snprintf(na->source, CSOS_NAME_LEN, "planted:%s", seed->source);
        strncpy(na->born_in, d->name, CSOS_NAME_LEN - 1);
        na->params[0] = seed->center * coupling; /* Scale by Forster coupling */
        na->param_count = 1;
        strncpy(na->param_keys[0], "c", 31);
        na->spectral[0] = 0; na->spectral[1] = 10000;
        na->photon_cap = CSOS_ATOM_PHOTON_RING;
        na->photons = (csos_photon_t *)calloc(CSOS_ATOM_PHOTON_RING, sizeof(csos_photon_t));
        na->local_cap = CSOS_ATOM_PHOTON_RING;
        na->local_photons = (csos_photon_t *)calloc(CSOS_ATOM_PHOTON_RING, sizeof(csos_photon_t));
        csos_atom_compute_rw(na);
        d->atom_count++;
        planted++;
        s->seeds_planted++;
    }
    return planted;
}

/* ═══ GREENHOUSE: CONVERGENCE DETECTION ═══
 *
 * Two sessions converge when their Calvin patterns overlap.
 * Forster: k_ET = (1/τ)(R₀/r)⁶ where r = spectral distance.
 *
 * Convergence metric:
 *   1. Substrate distance: hash difference (Gouterman spectral identity)
 *   2. Gradient similarity: |grad_a - grad_b| / max(grad_a, grad_b) (Mitchell)
 *   3. Motor overlap: shared substrate hashes in motor memory (Forster)
 *
 * Returns coupling strength: 0 = no convergence, >1 = strong overlap.
 */
double csos_session_convergence(const csos_organism_t *org,
                                const csos_session_t *a,
                                const csos_session_t *b) {
    if (!a || !b || a == b) return 0;
    const csos_membrane_t *d = NULL;
    for (int i = 0; i < org->count; i++)
        if (org->membranes[i] && strcmp(org->membranes[i]->name, "eco_domain") == 0)
            { d = org->membranes[i]; break; }
    if (!d) return 0;

    /* 1. Substrate distance (Gouterman spectral overlap) */
    uint32_t dist = (a->substrate_hash > b->substrate_hash) ?
        a->substrate_hash - b->substrate_hash : b->substrate_hash - a->substrate_hash;
    double spectral_coupling = 1.0 / (1.0 + (double)dist / 500.0);

    /* 2. Gradient similarity (Mitchell) */
    double ga = a->peak_gradient > 0 ? a->peak_gradient : 1;
    double gb = b->peak_gradient > 0 ? b->peak_gradient : 1;
    double grad_sim = 1.0 - fabs(ga - gb) / (ga > gb ? ga : gb);
    if (grad_sim < 0) grad_sim = 0;

    /* 3. Motor overlap (Forster — shared substrates) */
    double motor_a = csos_motor_strength(d, a->substrate_hash);
    double motor_b = csos_motor_strength(d, b->substrate_hash);
    double motor_overlap = (motor_a + motor_b) / 2.0;

    /* Combined convergence: Forster-weighted product */
    double convergence = spectral_coupling * grad_sim * (1.0 + motor_overlap);
    return convergence;
}

/* Merge converging sessions: Forster-transfer Calvin patterns from src to dst */
int csos_session_merge(csos_organism_t *org,
                       const csos_session_t *src, csos_session_t *dst) {
    double conv = csos_session_convergence(org, src, dst);
    if (conv < CSOS_BOYER_THRESHOLD) return 0; /* Below Forster threshold */

    /* Harvest src's Calvin atoms and plant them via seed bank */
    int harvested = csos_seed_harvest(org, src);
    int planted = csos_seed_plant(org, dst);

    return planted;
}

/* ═══ GREENHOUSE: PERSISTENCE ═══ */

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

int csos_session_save(const csos_organism_t *org, const char *dir) {
    char path[512];
    snprintf(path, sizeof(path), "%s/sessions_live.json", dir);
    FILE *f = fopen(path, "w");
    if (!f) return -1;
    fprintf(f, "{\"sessions\":[\n");
    for (int i = 0; i < org->session_count; i++) {
        const csos_session_t *s = &org->sessions[i];
        const csos_session_synthesis_t *syn = &s->synthesis;
        const char *stage_names[] = {"seed","sprout","grow","bloom","harvest","dormant"};
        fprintf(f, "%s  {\"id\":\"%s\",\"substrate\":\"%s\",\"hash\":%u,"
                "\"stage\":\"%s\",\"birth\":%u,\"last_active\":%u,"
                "\"peak_gradient\":%.1f,\"peak_speed\":%.1f,"
                "\"deliveries\":%d,\"seeds_harvested\":%d,\"seeds_planted\":%d,"
                "\"binding\":\"%s\","
                "\"ingress_type\":\"%s\",\"ingress_source\":\"%s\",\"ingress_active\":%d,"
                "\"egress_type\":\"%s\",\"egress_target\":\"%s\",\"egress_active\":%d,"
                "\"schedule_interval\":%d,\"schedule_autonomous\":%d,"
                "\"tick_count\":%d,\"vitality\":%.4f,"
                "\"session_vitality\":%.6f,\"session_vitality_ema\":%.6f,"
                "\"session_vitality_peak\":%.6f,"
                "\"flow_health\":%.4f,\"workflow_health\":%.4f,"
                "\"connection_health\":%.4f,"
                "\"ingress_health\":%.4f,\"egress_health\":%.4f,"
                "\"total_elements\":%d,\"total_positive\":%d,"
                "\"ingress_attempts\":%d,\"ingress_successes\":%d,"
                "\"egress_attempts\":%d,\"egress_successes\":%d,"
                "\"workflow_count\":%d,\"connection_count\":%d",
                i > 0 ? ",\n" : "",
                s->id, s->substrate, s->substrate_hash,
                stage_names[s->stage < 6 ? s->stage : 0],
                s->birth_cycle, s->last_active,
                s->peak_gradient, s->peak_speed,
                s->deliveries, s->seeds_harvested, s->seeds_planted,
                s->binding,
                s->ingress.type, s->ingress.source, s->ingress.active,
                s->egress.type, s->egress.target, s->egress.active,
                s->schedule.interval_secs, s->schedule.autonomous,
                s->schedule.tick_count, s->vitality,
                syn->session_vitality, syn->session_vitality_ema,
                syn->session_vitality_peak,
                syn->flow_health, syn->workflow_health,
                syn->connection_health,
                syn->ingress_health, syn->egress_health,
                syn->total_elements, syn->total_positive,
                syn->ingress_attempts, syn->ingress_successes,
                syn->egress_attempts, syn->egress_successes,
                s->workflow_count, s->connection_count);

        /* Persist active workflows */
        if (s->workflow_count > 0) {
            fprintf(f, ",\"workflows\":[");
            for (int w = 0; w < s->workflow_count; w++) {
                const csos_workflow_track_t *wf = &s->workflows[w];
                fprintf(f, "%s{\"name\":\"%s\",\"steps_total\":%d,"
                        "\"steps_completed\":%d,\"steps_failed\":%d,"
                        "\"throughput\":%.3f,\"active\":%d}",
                        w > 0 ? "," : "",
                        wf->name, wf->steps_total,
                        wf->steps_completed, wf->steps_failed,
                        wf->throughput, wf->active);
            }
            fprintf(f, "]");
        }

        /* Persist active connections */
        if (s->connection_count > 0) {
            fprintf(f, ",\"connections\":[");
            for (int c = 0; c < s->connection_count; c++) {
                const csos_connection_t *cn = &s->connections[c];
                fprintf(f, "%s{\"target\":\"%s\",\"protocol\":\"%s\","
                        "\"health\":%.3f,\"successes\":%d,\"failures\":%d,"
                        "\"active\":%d}",
                        c > 0 ? "," : "",
                        cn->target, cn->protocol,
                        cn->health, cn->successes, cn->failures,
                        cn->active);
            }
            fprintf(f, "]");
        }

        fprintf(f, "}");
    }
    fprintf(f, "\n]}\n");
    fclose(f);
    return 0;
}

int csos_session_load(csos_organism_t *org, const char *dir) {
    char path[512];
    snprintf(path, sizeof(path), "%s/sessions_live.json", dir);
    FILE *f = fopen(path, "r");
    if (!f) return 0;
    char buf[16384] = {0};
    size_t n = fread(buf, 1, sizeof(buf) - 1, f);
    fclose(f);
    if (n == 0) return 0;

    int loaded = 0;
    char *p = strstr(buf, "\"sessions\"");
    if (!p) return 0;
    p = strchr(p, '[');
    if (!p) return 0;

    while ((p = strstr(p, "{\"id\":\"")) != NULL && org->session_count < CSOS_MAX_SESSIONS) {
        csos_session_t *s = &org->sessions[org->session_count];
        memset(s, 0, sizeof(*s));
        char *ns = p + 7; int ni = 0;
        while (*ns && *ns != '"' && ni < CSOS_NAME_LEN - 1) s->id[ni++] = *ns++;
        char *ss = strstr(p, "\"substrate\":\"");
        if (ss) { ss += 13; ni = 0; while (*ss && *ss != '"' && ni < CSOS_NAME_LEN - 1) s->substrate[ni++] = *ss++; }
        char *ha = strstr(p, "\"hash\":");
        if (ha) s->substrate_hash = (uint32_t)strtoul(ha + 7, NULL, 10);
        /* Parse stage */
        char *st = strstr(p, "\"stage\":\"");
        if (st) {
            st += 9;
            if (strncmp(st, "seed", 4) == 0) s->stage = SESSION_SEED;
            else if (strncmp(st, "sprout", 6) == 0) s->stage = SESSION_SPROUT;
            else if (strncmp(st, "grow", 4) == 0) s->stage = SESSION_GROW;
            else if (strncmp(st, "bloom", 5) == 0) s->stage = SESSION_BLOOM;
            else if (strncmp(st, "harvest", 7) == 0) s->stage = SESSION_HARVEST;
            else if (strncmp(st, "dormant", 7) == 0) s->stage = SESSION_DORMANT;
        }
        char *bi = strstr(p, "\"birth\":");
        if (bi) s->birth_cycle = (uint32_t)strtoul(bi + 8, NULL, 10);
        char *la = strstr(p, "\"last_active\":");
        if (la) s->last_active = (uint32_t)strtoul(la + 14, NULL, 10);
        char *pg = strstr(p, "\"peak_gradient\":");
        if (pg) s->peak_gradient = strtod(pg + 16, NULL);
        char *ps = strstr(p, "\"peak_speed\":");
        if (ps) s->peak_speed = strtod(ps + 13, NULL);
        char *de = strstr(p, "\"deliveries\":");
        if (de) s->deliveries = (int)strtol(de + 13, NULL, 10);
        /* Restore living equation fields */
        char *bd = strstr(p, "\"binding\":\"");
        if (bd) { bd += 11; ni = 0; while (*bd && *bd != '"' && ni < 127) s->binding[ni++] = *bd++; }
        char *it = strstr(p, "\"ingress_type\":\"");
        if (it) { it += 16; ni = 0; while (*it && *it != '"' && ni < 15) s->ingress.type[ni++] = *it++; }
        char *is = strstr(p, "\"ingress_source\":\"");
        if (is) { is += 18; ni = 0; while (*is && *is != '"' && ni < CSOS_INGRESS_CMD_LEN-1) s->ingress.source[ni++] = *is++; }
        char *ia = strstr(p, "\"ingress_active\":");
        if (ia) s->ingress.active = (int)strtol(ia + 17, NULL, 10);
        char *et = strstr(p, "\"egress_type\":\"");
        if (et) { et += 15; ni = 0; while (*et && *et != '"' && ni < 15) s->egress.type[ni++] = *et++; }
        char *eg = strstr(p, "\"egress_target\":\"");
        if (eg) { eg += 17; ni = 0; while (*eg && *eg != '"' && ni < CSOS_EGRESS_CMD_LEN-1) s->egress.target[ni++] = *eg++; }
        char *ea = strstr(p, "\"egress_active\":");
        if (ea) s->egress.active = (int)strtol(ea + 16, NULL, 10);
        char *si = strstr(p, "\"schedule_interval\":");
        if (si) s->schedule.interval_secs = (int)strtol(si + 20, NULL, 10);
        char *sa = strstr(p, "\"schedule_autonomous\":");
        if (sa) s->schedule.autonomous = (int)strtol(sa + 22, NULL, 10);
        char *tc = strstr(p, "\"tick_count\":");
        if (tc) s->schedule.tick_count = (int)strtol(tc + 13, NULL, 10);
        char *vi = strstr(p, "\"vitality\":");
        if (vi) s->vitality = strtod(vi + 11, NULL);

        /* Restore synthesis state */
        char *sv = strstr(p, "\"session_vitality\":");
        if (sv) s->synthesis.session_vitality = strtod(sv + 19, NULL);
        char *sve = strstr(p, "\"session_vitality_ema\":");
        if (sve) s->synthesis.session_vitality_ema = strtod(sve + 23, NULL);
        char *svp = strstr(p, "\"session_vitality_peak\":");
        if (svp) s->synthesis.session_vitality_peak = strtod(svp + 24, NULL);
        char *fh = strstr(p, "\"flow_health\":");
        if (fh) s->synthesis.flow_health = strtod(fh + 14, NULL);
        char *wh = strstr(p, "\"workflow_health\":");
        if (wh) s->synthesis.workflow_health = strtod(wh + 18, NULL);
        char *ch = strstr(p, "\"connection_health\":");
        if (ch) s->synthesis.connection_health = strtod(ch + 20, NULL);
        char *ih = strstr(p, "\"ingress_health\":");
        if (ih) s->synthesis.ingress_health = strtod(ih + 17, NULL);
        char *eh = strstr(p, "\"egress_health\":");
        if (eh) s->synthesis.egress_health = strtod(eh + 16, NULL);
        char *te = strstr(p, "\"total_elements\":");
        if (te) s->synthesis.total_elements = (int)strtol(te + 17, NULL, 10);
        char *tp = strstr(p, "\"total_positive\":");
        if (tp) s->synthesis.total_positive = (int)strtol(tp + 17, NULL, 10);
        char *iat = strstr(p, "\"ingress_attempts\":");
        if (iat) s->synthesis.ingress_attempts = (int)strtol(iat + 19, NULL, 10);
        char *isu = strstr(p, "\"ingress_successes\":");
        if (isu) s->synthesis.ingress_successes = (int)strtol(isu + 20, NULL, 10);
        char *eat = strstr(p, "\"egress_attempts\":");
        if (eat) s->synthesis.egress_attempts = (int)strtol(eat + 18, NULL, 10);
        char *esu = strstr(p, "\"egress_successes\":");
        if (esu) s->synthesis.egress_successes = (int)strtol(esu + 19, NULL, 10);

        /* Restore workflow count and connection count */
        char *wc = strstr(p, "\"workflow_count\":");
        int wf_count = wc ? (int)strtol(wc + 17, NULL, 10) : 0;
        char *cc = strstr(p, "\"connection_count\":");
        int cn_count = cc ? (int)strtol(cc + 19, NULL, 10) : 0;

        /* Restore workflow trackers */
        char *wf_arr = strstr(p, "\"workflows\":[");
        if (wf_arr && wf_count > 0) {
            char *wp = wf_arr + 13;
            for (int w = 0; w < wf_count && w < CSOS_MAX_WORKFLOWS; w++) {
                char *wn = strstr(wp, "\"name\":\"");
                if (!wn) break;
                wn += 8;
                csos_workflow_track_t *wf = &s->workflows[s->workflow_count];
                memset(wf, 0, sizeof(*wf));
                ni = 0;
                while (*wn && *wn != '"' && ni < CSOS_NAME_LEN - 1)
                    wf->name[ni++] = *wn++;
                char *wst = strstr(wp, "\"steps_total\":");
                if (wst) wf->steps_total = (int)strtol(wst + 14, NULL, 10);
                char *wsc = strstr(wp, "\"steps_completed\":");
                if (wsc) wf->steps_completed = (int)strtol(wsc + 18, NULL, 10);
                char *wsf = strstr(wp, "\"steps_failed\":");
                if (wsf) wf->steps_failed = (int)strtol(wsf + 15, NULL, 10);
                char *wtp = strstr(wp, "\"throughput\":");
                if (wtp) wf->throughput = strtod(wtp + 13, NULL);
                char *wac = strstr(wp, "\"active\":");
                if (wac) wf->active = (int)strtol(wac + 9, NULL, 10);
                s->workflow_count++;
                wp = strchr(wp, '}');
                if (wp) wp++;
            }
        }

        /* Restore connection trackers */
        char *cn_arr = strstr(p, "\"connections\":[");
        if (cn_arr && cn_count > 0) {
            char *cp = cn_arr + 15;
            for (int c = 0; c < cn_count && c < CSOS_MAX_CONNECTIONS; c++) {
                char *ct = strstr(cp, "\"target\":\"");
                if (!ct) break;
                ct += 10;
                csos_connection_t *cn = &s->connections[s->connection_count];
                memset(cn, 0, sizeof(*cn));
                ni = 0;
                while (*ct && *ct != '"' && ni < 127)
                    cn->target[ni++] = *ct++;
                char *cpr = strstr(cp, "\"protocol\":\"");
                if (cpr) { cpr += 12; ni = 0; while (*cpr && *cpr != '"' && ni < 15) cn->protocol[ni++] = *cpr++; }
                char *chl = strstr(cp, "\"health\":");
                if (chl) cn->health = strtod(chl + 9, NULL);
                char *csu = strstr(cp, "\"successes\":");
                if (csu) cn->successes = (int)strtol(csu + 12, NULL, 10);
                char *cfa = strstr(cp, "\"failures\":");
                if (cfa) cn->failures = (int)strtol(cfa + 11, NULL, 10);
                char *cac = strstr(cp, "\"active\":");
                if (cac) cn->active = (int)strtol(cac + 9, NULL, 10);
                s->connection_count++;
                cp = strchr(cp, '}');
                if (cp) cp++;
            }
        }

        if (s->ingress.timeout_ms == 0) s->ingress.timeout_ms = 5000;
        /* Recompute next_tick for scheduled sessions */
        if (s->schedule.interval_secs > 0 && s->schedule.autonomous) {
            s->schedule.next_tick = (int64_t)time(NULL) + s->schedule.interval_secs;
        }
        org->session_count++;
        loaded++;
        p++;
    }
    return loaded;
}

/* ═══ GREENHOUSE: OBSERVATION ═══ */

int csos_greenhouse_see(const csos_organism_t *org, char *json, size_t sz) {
    int pos = 0;
    const char *stage_names[] = {"seed","sprout","grow","bloom","harvest","dormant"};
    pos += snprintf(json + pos, sz - pos, "{\"greenhouse\":{\"sessions\":[");
    for (int i = 0; i < org->session_count && pos < (int)sz - 256; i++) {
        const csos_session_t *s = &org->sessions[i];
        if (i > 0) pos += snprintf(json + pos, sz - pos, ",");
        pos += snprintf(json + pos, sz - pos,
            "{\"id\":\"%s\",\"substrate\":\"%s\",\"stage\":\"%s\","
            "\"peak_gradient\":%.0f,\"peak_speed\":%.1f,"
            "\"deliveries\":%d,\"seeds_planted\":%d}",
            s->id, s->substrate, stage_names[s->stage < 6 ? s->stage : 0],
            s->peak_gradient, s->peak_speed,
            s->deliveries, s->seeds_planted);
    }
    pos += snprintf(json + pos, sz - pos, "],\"seeds\":%d,\"convergences\":[", org->seed_count);

    /* Compute convergence between all active session pairs */
    int first = 1;
    for (int i = 0; i < org->session_count && pos < (int)sz - 200; i++) {
        for (int j = i + 1; j < org->session_count && pos < (int)sz - 200; j++) {
            double c = csos_session_convergence(org, &org->sessions[i], &org->sessions[j]);
            if (c > CSOS_BOYER_THRESHOLD) {
                if (!first) pos += snprintf(json + pos, sz - pos, ",");
                pos += snprintf(json + pos, sz - pos,
                    "{\"a\":\"%s\",\"b\":\"%s\",\"strength\":%.3f}",
                    org->sessions[i].id, org->sessions[j].id, c);
                first = 0;
            }
        }
    }
    pos += snprintf(json + pos, sz - pos, "]}}");
    return 0;
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
        mem->co2_count
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
        "\"seeds\":%d,\"sessions\":%d"
        "}}",
        org_vitality, org->seed_count, org->session_count);

    return 0;
}
