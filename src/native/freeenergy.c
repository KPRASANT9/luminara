/*
 * CSOS V14 Free Energy — Dual-Reactor Photosynthetic Architecture
 *
 * F = COMPLEXITY - ACCURACY. dF/dt <= 0 at all times.
 *
 * V14 changes from V13:
 *   - LIGHT REACTOR: Gouterman + Forster + Marcus = complexity reduction
 *     Produces ATP (per resonation) and NADPH (per error < lambda)
 *   - DARK REACTOR: Mitchell + Boyer + Calvin = accuracy maximization
 *     Consumes ATP + NADPH to build predictive models
 *   - COUPLING: Explicit ATP/NADPH budget prevents dark from outrunning light
 *   - SUBSTRATE REFLEXIVITY: Propagates self-impact to ALL atoms on substrate
 *   - REACTOR BALANCE: Boyer scales decision confidence by light/dark ratio
 *   - PHOTOPROTECTION: NPQ dissipates excess energy when ATP overflows
 *
 * 14 mechanisms (7 light, 7 dark). Same 5 equations. Two reactors. One membrane.
 */

#include "../../lib/membrane.h"
#include <math.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

/* ================================================================
 * SECTION 1: F DECOMPOSITION
 * ================================================================ */

static void freeenergy_decompose(csos_membrane_t *m) {
    m->F_accuracy = 1.0 / (1.0 + m->F);
    double complexity = 0;
    static const double hierarchy_compression[] = {1.0, 0.1, 0.02, 0.002, 0.02};
    for (int i = 0; i < m->atom_count; i++) {
        int level = m->atoms[i].hierarchy_level;
        if (level > 4) level = 4;
        complexity += hierarchy_compression[level];
    }
    m->F_complexity = complexity / (double)CSOS_MAX_ATOMS;
    m->F_free_energy = m->F_complexity - m->F_accuracy;
    m->dF_dt = m->F - m->F_prev;
    m->F_prev = m->F;
    if (m->dF_dt > 0) m->dF_positive_streak++;
    else m->dF_positive_streak = 0;
}

/* ================================================================
 * SECTION 2: LIGHT REACTOR — Complexity Reduction
 *
 * Equations: Gouterman + Forster + Marcus
 * Produces: ATP (per resonation), NADPH (per error < lambda)
 * Role: absorb, filter, transfer, gate. Does NOT build models.
 * ================================================================ */

/* LIGHT-1: Info decay (Gap 2) */
static void light_info_decay(csos_membrane_t *m) {
    for (int i = 0; i < m->atom_count; i++) {
        csos_atom_t *a = &m->atoms[i];
        if (a->info_half_life <= 0) {
            a->info_half_life = csos_derive_info_half_life(a->spectral[0], a->spectral[1]);
            a->info_remaining = 1.0;
        }
        double decay = exp(-0.693 / a->info_half_life);
        a->info_remaining *= decay;
        if (a->info_remaining < 0.01) a->info_remaining = 0.01;
        if (a->has_resonated && a->photon_count > 0) {
            int last = (a->photon_head - 1) & (a->photon_cap - 1);
            if (a->photons[last].resonated && a->photons[last].cycle == m->cycles)
                a->info_remaining = 1.0;
        }
    }
}

/* LIGHT-2: Self-impact (Gap 1) — V14: substrate-level propagation */
static void light_self_impact(csos_membrane_t *m, const csos_photon_t *ph) {
    if (ph->protocol != PROTO_LLM && ph->protocol != PROTO_HTTP) return;
    for (int i = 0; i < m->motor_count; i++) {
        if (m->motor[i].substrate_hash != ph->substrate_hash) continue;
        double expected = 0.1 * (1.0 - m->motor[i].strength) + 0.01;
        if (m->motor[i].reps < 5) expected = 0.01;
        double observed = fabs(ph->actual - ph->predicted);
        double alpha = 0.1;
        double ratio = observed / (expected + 1e-10);
        double prev = m->substrate_reflexivity[i];
        double new_r = (prev > 0.1 ? prev : 1.0) * (1 - alpha) + ratio * alpha;
        if (new_r > 10.0) new_r = 10.0;
        if (new_r < 0.1) new_r = 0.1;
        m->substrate_reflexivity[i] = new_r;
        for (int j = 0; j < m->atom_count; j++) {
            csos_atom_t *a = &m->atoms[j];
            if (a->causal_target == ph->substrate_hash ||
                a->upstream_hash == ph->substrate_hash ||
                a->downstream_hash == ph->substrate_hash) {
                a->self_impact_expected = a->self_impact_expected * (1 - alpha) + expected * alpha;
                a->self_impact_observed = a->self_impact_observed * (1 - alpha) + observed * alpha;
                a->reflexivity_ratio = new_r;
            }
        }
        break;
    }
}

/* LIGHT-3: Order book (Gap 7) */
static void light_parse_orderbook(csos_membrane_t *m, const csos_photon_t *ph) {
    (void)ph;
    for (int i = 0; i < m->atom_count; i++) {
        csos_atom_t *a = &m->atoms[i];
        if (a->spectral_band != 0 || !a->has_resonated) continue;
        if (a->bid_depth > 0 || a->ask_depth > 0) {
            double total = a->bid_depth + a->ask_depth + 1e-10;
            a->book_imbalance = (a->bid_depth - a->ask_depth) / total;
        } else {
            a->book_imbalance = 0;
        }
    }
}

/* LIGHT-4: Spectral weighting (Gap 3) */
static void light_spectral_weight(csos_membrane_t *m) {
    for (int i = 0; i < m->atom_count; i++) {
        csos_atom_t *a = &m->atoms[i];
        if (a->spectral_weight > 0) continue;
        double mid = (a->spectral[0] + a->spectral[1]) / 2.0;
        if (mid < 100) a->spectral_band = 0;
        else if (mid < 1000) a->spectral_band = 1;
        else a->spectral_band = 2;
        a->spectral_weight = csos_derive_spectral_weight(a->spectral[0], a->spectral[1]);
    }
}

/* LIGHT-5: Agent-type classification (Gap 5) */
static void light_classify_agent_type(csos_membrane_t *m) {
    double p25, p75, p99;
    csos_derive_agent_quantiles(m, &p25, &p75, &p99);
    for (int i = 0; i < m->motor_count; i++) {
        csos_motor_t *motor = &m->motor[i];
        if (motor->reps < 5) continue;
        uint8_t at = 0;
        if (motor->interval < (uint64_t)p25 && motor->strength > 0.5) at = 1;
        else if (motor->interval > (uint64_t)p99) at = 4;
        else if (motor->interval > (uint64_t)p75 && motor->strength > 0.3) at = 2;
        else if (motor->interval >= (uint64_t)p25 && motor->interval <= (uint64_t)p75) at = 3;
        for (int j = 0; j < m->atom_count; j++) {
            if (m->atoms[j].causal_target == motor->substrate_hash ||
                m->atoms[j].upstream_hash == motor->substrate_hash) {
                m->atoms[j].agent_type = at;
                m->atoms[j].agent_signature = (double)motor->interval / 100.0;
            }
        }
    }
}

/* LIGHT-6: Adversarial decay (Gap 4) */
static void light_adversarial_decay(csos_membrane_t *m) {
    uint32_t targets[CSOS_MAX_ATOMS]; int tcnt[CSOS_MAX_ATOMS]; int ut = 0;
    for (int i = 0; i < m->atom_count; i++) {
        if (m->atoms[i].causal_target == 0) continue;
        int f = -1;
        for (int j = 0; j < ut; j++) if (targets[j] == m->atoms[i].causal_target) { f = j; break; }
        if (f >= 0) tcnt[f]++;
        else if (ut < CSOS_MAX_ATOMS) { targets[ut] = m->atoms[i].causal_target; tcnt[ut] = 1; ut++; }
    }
    for (int i = 0; i < m->atom_count; i++) {
        csos_atom_t *a = &m->atoms[i];
        if (a->edge_strength <= 0) continue;
        for (int j = 0; j < ut; j++) {
            if (targets[j] == a->causal_target) {
                a->crowding_score = (double)(tcnt[j] - 1) / (double)m->atom_count;
                break;
            }
        }
        a->edge_decay_rate = a->edge_strength * a->crowding_score;
        double ar = 1.0 / (double)(m->atom_count > 1 ? m->atom_count : 1);
        a->causal_strength *= (1.0 - a->edge_decay_rate * ar);
        if (a->causal_strength < ar) a->causal_strength = ar;
        double ep = a->info_half_life > 0 ? exp(-1.0 / a->info_half_life) : 0.999;
        a->edge_strength *= ep;
    }
}

/* LIGHT-7: Photoprotection (V14 NEW) — dissipate excess ATP */
static int light_photoprotection(csos_membrane_t *m) {
    double overflow = (double)CSOS_MAX_ATOMS * (double)m->mitchell_n;
    if (m->atp_pool <= overflow) { m->npq_active = 0; return 0; }
    m->npq_active = 1;
    int pruned = 0;
    for (int i = m->atom_count - 1; i >= 5; i--) {
        csos_atom_t *a = &m->atoms[i];
        if (a->hierarchy_level != 0 || a->net_value >= 0) continue;
        m->npq_dissipated += a->info_remaining * a->spectral_weight;
        m->atp_pool -= a->info_remaining;
        if (a->photons) free(a->photons);
        if (a->local_photons) free(a->local_photons);
        if (i < m->atom_count - 1) m->atoms[i] = m->atoms[m->atom_count - 1];
        m->atom_count--; pruned++;
        if (m->atp_pool <= overflow) break;
    }
    return pruned;
}

/* LIGHT REACTOR MASTER */
static void freeenergy_light_reactor(csos_membrane_t *m, const csos_photon_t *ph) {
    m->atp_produced_this_cycle = 0;
    m->nadph_produced_this_cycle = 0;

    /* ATP: 1 per resonated photon (Gouterman) */
    if (ph->resonated) {
        m->atp_pool += 1.0;
        m->atp_produced_this_cycle += 1.0;
    }
    /* NADPH: 1 per low-error signal (Marcus gate) */
    double lambda = (m->atom_count > 0) ? csos_derive_error_guard(&m->atoms[0]) : 0.1;
    if (ph->error < lambda && ph->resonated) {
        m->nadph_pool += 1.0;
        m->nadph_produced_this_cycle += 1.0;
    }

    /* Always-run mechanisms */
    light_info_decay(m);
    light_self_impact(m, ph);
    light_parse_orderbook(m, ph);

    /* Adaptive mechanisms */
    int behind = (m->dF_dt > 0) || (m->dF_positive_streak >= 2);
    if (behind || m->cycles % 10 == 0) {
        light_spectral_weight(m);
        light_classify_agent_type(m);
    }
    if (behind || m->cycles % 20 == 0)
        light_adversarial_decay(m);

    light_photoprotection(m);
    m->light_cycles++;
}

/* ================================================================
 * SECTION 3: DARK REACTOR — Accuracy Maximization
 *
 * Equations: Mitchell + Boyer
 * Consumes: ATP + NADPH from light reactor
 * Role: build patterns, discover causality, classify regimes, decide.
 * CANNOT run without energy from light reactor.
 * ================================================================ */

/* DARK-1: Causal injection (Do-calculus L2) — 1 ATP per inject */
static int _cid = 0;
static int dark_causal_inject(csos_membrane_t *m, const csos_photon_t *ph) {
    if (_cid > 0) return 0;
    _cid++;
    int inj = 0;
    for (int i = 0; i < m->atom_count; i++) {
        csos_atom_t *a = &m->atoms[i];
        if (!a->causal_target || !a->has_resonated || !a->causal_lag || !a->photon_count) continue;
        int last = (a->photon_head - 1) & (a->photon_cap - 1);
        if (!a->photons[last].resonated || a->photons[last].cycle != ph->cycle) continue;
        if (ph->protocol == PROTO_INTERNAL || ph->protocol == PROTO_LLM) {
            a->intervention_count++; continue;
        }
        if (m->atp_pool < 1.0) continue;
        m->atp_pool -= 1.0; m->atp_consumed_this_cycle += 1.0;
        double pred = a->last_resonated_value * a->causal_strength;
        a->has_pending = 1; a->pending_value = pred;
        csos_membrane_absorb(m, pred, a->causal_target, PROTO_INTERNAL);
        inj++;
    }
    _cid--;
    return inj;
}

/* DARK-2: Hierarchy assignment — 1 ATP per atom */
static void dark_assign_hierarchy(csos_membrane_t *m) {
    for (int i = 0; i < m->atom_count; i++) {
        if (m->atp_pool < 1.0) break;
        m->atp_pool -= 1.0; m->atp_consumed_this_cycle += 1.0;
        csos_atom_t *a = &m->atoms[i];
        if (a->rhythm_period > 0) { a->hierarchy_level = 4; continue; }
        if (a->causal_target > 0) { a->hierarchy_level = 3; continue; }
        if (a->photon_count > 100 && a->has_resonated) {
            uint32_t seen[16] = {0}; int u = 0;
            int chk = a->photon_count < 64 ? a->photon_count : 64;
            for (int j = 0; j < chk; j++) {
                int idx = (a->photon_head - 1 - j) & (a->photon_cap - 1);
                uint32_t sh = a->photons[idx].substrate_hash;
                int found = 0;
                for (int k = 0; k < u; k++) if (seen[k] == sh) { found = 1; break; }
                if (!found && u < 16) seen[u++] = sh;
            }
            if (u >= 4) { a->hierarchy_level = 2; continue; }
        }
        if (strncmp(a->name, "calvin_", 7) == 0) { a->hierarchy_level = 1; continue; }
        a->hierarchy_level = 0;
    }
}

/* DARK-3: Rhythm detection — 2 ATP per period test */
static void dark_detect_rhythms(csos_membrane_t *m) {
    for (int i = 0; i < m->atom_count; i++) {
        csos_atom_t *a = &m->atoms[i];
        if (a->photon_count < 20 || a->rhythm_period > 0) continue;
        int best_p = 0; double best_s = 0;
        int n = a->photon_count < (int)a->photon_cap ? a->photon_count : (int)a->photon_cap;
        if (n < 20) continue;
        for (int p = 3; p <= 100 && p < n / 2; p++) {
            if (m->atp_pool < 2.0) break;
            m->atp_pool -= 2.0; m->atp_consumed_this_cycle += 2.0;
            int mat = 0, chk = 0;
            for (int j = 0; j < n - p && chk < 50; j++) {
                int ia = (a->photon_head - 1 - j) & (a->photon_cap - 1);
                int ib = (a->photon_head - 1 - j - p) & (a->photon_cap - 1);
                if (a->photons[ia].resonated && a->photons[ib].resonated) mat++;
                chk++;
            }
            double sc = chk > 0 ? (double)mat / chk : 0;
            if (sc > best_s && sc > (1.0 - a->rw)) { best_s = sc; best_p = p; }
        }
        if (best_p > 0) { a->rhythm_period = (uint16_t)best_p; a->rhythm_amplitude = best_s; a->hierarchy_level = 4; }
    }
}

/* DARK-4: Pruning (L1 regularization) */
static int dark_prune(csos_membrane_t *m, int interval) {
    if (m->cycles == 0 || m->cycles % interval != 0) return 0;
    if (m->atom_count <= 5) return 0;
    for (int i = 0; i < m->atom_count; i++) {
        csos_atom_t *a = &m->atoms[i];
        int res = 0, tot = 0; double esum = 0;
        int n = a->photon_count < (int)a->photon_cap ? a->photon_count : (int)a->photon_cap;
        for (int j = 0; j < n && j < 50; j++) {
            int idx = (a->photon_head - 1 - j) & (a->photon_cap - 1);
            tot++;
            if (a->photons[idx].resonated) { res++; esum += a->photons[idx].error; }
        }
        double rr = tot > 0 ? (double)res / tot : 0;
        double ae = res > 0 ? esum / res : 1.0;
        a->accuracy_contribution = rr * (1.0 - ae);
        if (a->info_remaining < 1.0) a->accuracy_contribution *= a->info_remaining;
        static const double hc[] = {1.0, 0.1, 0.02, 0.002, 0.02};
        int lv = a->hierarchy_level > 4 ? 4 : a->hierarchy_level;
        a->complexity_cost = hc[lv] / (double)CSOS_MAX_ATOMS;
        a->net_value = a->accuracy_contribution - a->complexity_cost;
    }
    int pruned = 0;
    for (int i = m->atom_count - 1; i >= 5; i--) {
        if (m->atoms[i].net_value >= 0) continue;
        if (m->atoms[i].photons) free(m->atoms[i].photons);
        if (m->atoms[i].local_photons) free(m->atoms[i].local_photons);
        if (i < m->atom_count - 1) m->atoms[i] = m->atoms[m->atom_count - 1];
        m->atom_count--; pruned++;
    }
    return pruned;
}

/* DARK-5: Counterfactual scoring — 1 NADPH per validation */
static void dark_counterfactual(csos_membrane_t *m) {
    for (int i = 0; i < m->atom_count; i++) {
        csos_atom_t *a = &m->atoms[i];
        if (!a->causal_target || a->photon_count < 10) continue;
        if (m->nadph_pool < 1.0) continue;
        m->nadph_pool -= 1.0; m->nadph_consumed_this_cycle += 1.0;
        int cd = 0, tc = 0; double tme = 0;
        int n = a->photon_count < (int)a->photon_cap ? a->photon_count : (int)a->photon_cap;
        int chk = n < 20 ? n : 20;
        for (int j = 0; j < chk; j++) {
            int idx = (a->photon_head - 1 - j) & (a->photon_cap - 1);
            csos_photon_t *p = &a->photons[idx];
            if (!p->resonated) continue;
            tc++;
            if ((p->predicted > 0 ? 1.0 : -1.0) * (p->actual > 0 ? 1.0 : -1.0) > 0) cd++;
            tme += p->error;
        }
        if (tc == 0) continue;
        double da = (double)cd / tc, ae = tme / tc;
        double cr = csos_derive_cf_regime_threshold(tc);
        double cb = csos_derive_cf_boost_threshold(tc);
        if (da < cr) { a->counterfactual_score = 0.9; a->causal_strength *= 0.5; }
        else if (ae > (1.0 - cr)) { a->counterfactual_score = 0.2; a->causal_strength *= 0.8; }
        else {
            a->counterfactual_score = 0.5;
            if (da > cb) a->causal_strength *= 1.0 + cr;
            if (a->causal_strength > 1.0) a->causal_strength = 1.0;
        }
    }
}

/* DARK-6: Rhythm atom creation — 3 ATP */
static int dark_create_rhythm(csos_membrane_t *m) {
    if (m->atom_count >= CSOS_MAX_ATOMS - 1 || m->co2_count < 20 || m->atp_pool < 3.0) return 0;
    for (int p = 3; p <= 20 && p < m->co2_count / 2; p++) {
        int mat = 0, chk = 0;
        for (int j = 0; j + p < m->co2_count && chk < 30; j++) {
            double d = fabs(m->co2[j] - m->co2[j + p]);
            if (d / (fabs(m->co2[j]) + 1e-10) < (1.0 - m->rw)) mat++;
            chk++;
        }
        double sc = chk > 0 ? (double)mat / chk : 0;
        if (sc < 1.0 / (p + 1.0) + 0.1) continue;
        int ex = 0;
        for (int k = 0; k < m->atom_count; k++) if (m->atoms[k].rhythm_period == (uint16_t)p) { ex = 1; break; }
        if (ex) continue;
        m->atp_pool -= 3.0; m->atp_consumed_this_cycle += 3.0;
        csos_atom_t *na = &m->atoms[m->atom_count];
        memset(na, 0, sizeof(csos_atom_t));
        snprintf(na->name, CSOS_NAME_LEN, "rhythm_p%d_c%u", p, m->cycles);
        double sum = 0; int cnt = 0;
        for (int j = 0; j < m->co2_count; j += p) { sum += m->co2[j]; cnt++; }
        double mean = cnt > 0 ? sum / cnt : 0;
        snprintf(na->formula, CSOS_FORMULA_LEN, "rhythm@%.2f period=%d", mean, p);
        snprintf(na->compute, CSOS_FORMULA_LEN, "%.6f", mean);
        snprintf(na->source, CSOS_NAME_LEN, "Rhythm %s", m->name);
        strncpy(na->born_in, m->name, CSOS_NAME_LEN - 1);
        na->spectral[0] = 0; na->spectral[1] = 10000;
        na->rhythm_period = (uint16_t)p; na->rhythm_amplitude = sc; na->hierarchy_level = 4;
        na->info_half_life = (double)p * sc * 100.0; na->info_remaining = 1.0;
        na->photon_cap = CSOS_ATOM_PHOTON_RING;
        na->photons = (csos_photon_t *)calloc(CSOS_ATOM_PHOTON_RING, sizeof(csos_photon_t));
        na->local_cap = CSOS_ATOM_PHOTON_RING;
        na->local_photons = (csos_photon_t *)calloc(CSOS_ATOM_PHOTON_RING, sizeof(csos_photon_t));
        csos_atom_compute_rw(na);
        m->atom_count++;
        return 1;
    }
    return 0;
}

/* DARK-7: Regime HMM — 2 NADPH per update */
static void dark_regime_hmm(csos_membrane_t *m) {
    int best = -1, bestpc = 0;
    for (int i = 0; i < m->atom_count; i++) {
        csos_atom_t *a = &m->atoms[i];
        if (a->hierarchy_level != 2) continue;
        if (m->nadph_pool < 2.0) continue;
        m->nadph_pool -= 2.0; m->nadph_consumed_this_cycle += 2.0;
        if (a->transition_probs[0] == 0 && a->transition_probs[1] == 0) {
            a->transition_probs[0] = 0.995; a->transition_probs[1] = 0.004; a->transition_probs[2] = 0.001;
            a->current_regime = 0;
        }
        int n = a->photon_count < (int)a->photon_cap ? a->photon_count : (int)a->photon_cap;
        int rc = n < 10 ? n : 10;
        double re = 0; int rr = 0;
        for (int j = 0; j < rc; j++) {
            int idx = (a->photon_head - 1 - j) & (a->photon_cap - 1);
            re += a->photons[idx].error;
            if (a->photons[idx].resonated) rr++;
        }
        re = rc > 0 ? re / rc : 0;
        double ct = csos_derive_regime_crisis_threshold(m);
        double bt = csos_derive_regime_bear_threshold(m);
        uint8_t obs = re > ct ? 2 : (re > bt || rr < rc / 3) ? 1 : 0;
        double al = csos_derive_hmm_alpha(a->photon_count);
        for (int r = 0; r < 3; r++) {
            double tgt = (r == (int)obs) ? 1.0 : 0.0;
            a->transition_probs[r] = a->transition_probs[r] * (1.0 - al) + tgt * al;
        }
        a->current_regime = obs;
        if (a->photon_count > bestpc) { bestpc = a->photon_count; best = i; }
    }
    if (best >= 0) m->active_regime = m->atoms[best].current_regime;
}

/* DARK-8: Causal discovery — 5 ATP per link tested */
static int dark_discover_causal(csos_membrane_t *m) {
    if (m->atom_count >= CSOS_MAX_ATOMS - 1 || m->cycles < 50 || m->atp_pool < 5.0) return 0;
    for (int i = 0; i < m->motor_count && i < 20; i++) {
        for (int j = 0; j < m->motor_count && j < 20; j++) {
            if (i == j) continue;
            if (m->motor[i].substrate_hash == m->motor[j].substrate_hash) continue;
            if (m->motor[i].last_seen >= m->motor[j].last_seen) continue;
            uint64_t lag = m->motor[j].last_seen - m->motor[i].last_seen;
            if (lag == 0 || lag > 100) continue;
            if (m->motor[i].strength < 0.2 || m->motor[j].strength < 0.2) continue;
            int ex = 0;
            for (int k = 0; k < m->atom_count; k++)
                if (m->atoms[k].causal_target == m->motor[j].substrate_hash &&
                    m->atoms[k].params[0] == (double)m->motor[i].substrate_hash) { ex = 1; break; }
            if (ex) continue;
            m->atp_pool -= 5.0; m->atp_consumed_this_cycle += 5.0;
            csos_atom_t *na = &m->atoms[m->atom_count];
            memset(na, 0, sizeof(csos_atom_t));
            snprintf(na->name, CSOS_NAME_LEN, "causal_%u_%u", m->motor[i].substrate_hash, m->motor[j].substrate_hash);
            snprintf(na->formula, CSOS_FORMULA_LEN, "cause@%u->%u lag=%u",
                     m->motor[i].substrate_hash, m->motor[j].substrate_hash, (unsigned)lag);
            snprintf(na->compute, CSOS_FORMULA_LEN, "%.6f", m->motor[j].strength);
            snprintf(na->source, CSOS_NAME_LEN, "Causal %s", m->name);
            strncpy(na->born_in, m->name, CSOS_NAME_LEN - 1);
            na->params[0] = (double)m->motor[i].substrate_hash; na->param_count = 1;
            strncpy(na->param_keys[0], "cause_hash", 31);
            na->spectral[0] = 0; na->spectral[1] = 10000;
            na->causal_target = m->motor[j].substrate_hash;
            na->causal_lag = (uint16_t)lag;
            na->causal_strength = m->motor[i].strength * m->motor[j].strength;
            na->causal_direction = 1; na->hierarchy_level = 3;
            na->upstream_hash = m->motor[i].substrate_hash;
            na->downstream_hash = m->motor[j].substrate_hash;
            na->flow_lag = (uint16_t)lag; na->flow_strength = na->causal_strength;
            na->info_half_life = csos_derive_info_half_life(0, 10000);
            na->info_remaining = 1.0;
            na->self_impact_expected = 0.01; na->self_impact_observed = 0.01; na->reflexivity_ratio = 1.0;
            na->photon_cap = CSOS_ATOM_PHOTON_RING;
            na->photons = (csos_photon_t *)calloc(CSOS_ATOM_PHOTON_RING, sizeof(csos_photon_t));
            na->local_cap = CSOS_ATOM_PHOTON_RING;
            na->local_photons = (csos_photon_t *)calloc(CSOS_ATOM_PHOTON_RING, sizeof(csos_photon_t));
            csos_atom_compute_rw(na);
            m->atom_count++;
            return 1;
        }
    }
    return 0;
}

/* DARK-9: Active inference (free scan) */
static uint32_t dark_active_inference(const csos_membrane_t *m) {
    uint32_t bh = 0; double bs = 0;
    double fg = m->F > csos_derive_f_floor() ? m->F - csos_derive_f_floor() : 0;
    for (int i = 0; i < m->motor_count; i++) {
        const csos_motor_t *mt = &m->motor[i];
        if (mt->reps < 2) continue;
        double u = 1.0 - mt->strength;
        double s = mt->interval > 0 ? (double)mt->interval / 100.0 : 0;
        if (s > 1.0) s = 1.0;
        double sc = u * (1.0 + s) * (1.0 + fg);
        if (sc > bs) { bs = sc; bh = mt->substrate_hash; }
    }
    return bh;
}

/* DARK REACTOR MASTER */
static void freeenergy_dark_reactor(csos_membrane_t *m, const csos_photon_t *ph) {
    m->atp_consumed_this_cycle = 0;
    m->nadph_consumed_this_cycle = 0;

    dark_causal_inject(m, ph);

    int has_atp = (m->atp_pool > 1.0);
    int has_nadph = (m->nadph_pool > 1.0);
    int behind = (m->dF_dt > 0) || (m->dF_positive_streak >= 2);

    if (has_atp && (behind || m->cycles % 10 == 0)) {
        dark_assign_hierarchy(m);
        if (has_nadph) dark_regime_hmm(m);
    }
    if (has_atp && (behind || m->cycles % 50 == 0)) {
        dark_detect_rhythms(m);
        if (has_nadph) dark_counterfactual(m);
    }
    if (has_atp && (behind || m->cycles % 100 == 0)) {
        dark_create_rhythm(m);
        dark_discover_causal(m);
    }

    if (m->atom_count > CSOS_MAX_ATOMS - 5 || (behind && m->cycles % 100 == 0))
        dark_prune(m, 1);
    else
        dark_prune(m, 500);

    m->dark_cycles++;
}

/* ================================================================
 * SECTION 4: COUPLED POST-ABSORB
 * ================================================================ */

static uint32_t freeenergy_post_absorb(csos_membrane_t *m, const csos_photon_t *ph) {
    freeenergy_decompose(m);
    freeenergy_light_reactor(m, ph);
    freeenergy_dark_reactor(m, ph);

    /* Coupling update */
    double prod = m->atp_produced_this_cycle + 1e-10;
    double cons = m->atp_consumed_this_cycle + 1e-10;
    m->reactor_balance = m->reactor_balance * 0.95 + (prod / cons) * 0.05;

    double cap = (double)CSOS_MAX_ATOMS * m->mitchell_n * 2.0;
    if (m->atp_pool > cap) m->atp_pool = cap;
    if (m->nadph_pool > cap) m->nadph_pool = cap;
    if (m->atp_pool < 0) m->atp_pool = 0;
    if (m->nadph_pool < 0) m->nadph_pool = 0;

    return dark_active_inference(m);
}
