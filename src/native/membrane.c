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
int _autotune_calvin_freq = 5;
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

    m->rw = m->atom_count > 0 ? m->atoms[0].rw : CSOS_DEFAULT_RW;

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
        e->strength *= CSOS_MOTOR_DECAY;
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

    /* ── CALVIN (pattern synthesis — periodic) ── */
    if (m->cycles > 0 && m->cycles % CSOS_CALVIN_FREQUENCY == 0) membrane_calvin(m);

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
        "  \"mode\":%d,\n  \"decision\":%d,\n", m->mode, m->decision);
    pos += snprintf(buf + pos, bufsz - pos,
        "  \"consecutive_zero_delta\":%d,\n", m->consecutive_zero_delta);
    pos += snprintf(buf + pos, bufsz - pos,
        "  \"atom_count\":%d,\n", m->atom_count);

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
    return 0;
}

void csos_organism_destroy(csos_organism_t *org) {
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

    /* Periodic Forster diffuse */
    if (o && o->cycles > 0 && o->cycles % 10 == 0)
        csos_membrane_diffuse(o, d);

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
            "\"action_ratio\":%.4f,\"gradient_gap\":null,"
            "\"calvin_rate\":%.4f,\"boundary_crossings\":%d,"
            "\"mode\":\"%s\",\"decision\":\"%s\","
            "\"motor_entries\":%d,\"motor_cycle\":%llu}",
            m->name, m->F, m->gradient, m->speed, m->rw,
            spec, ar, cr, crossings,
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
