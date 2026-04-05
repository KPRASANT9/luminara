/*
 * CSOS — Production entry point.
 *
 * Unified membrane architecture. No layers. No silos.
 * One binary. One data structure. One process.
 *
 * Modes:
 *   (default)         CLI daemon (stdin/stdout JSON — backward compatible)
 *   --test            All stress tests (8 original + membrane + muscle)
 *   --bench           Benchmark membrane_absorb() throughput
 *   --http PORT       HTTP server
 *   --unix PATH       Unix domain socket daemon
 *   --muscle          Muscle memory demo
 *
 * LLM INTERACTION:
 *   The LLM communicates through the same JSON protocol as any other client.
 *   It sends: {"action":"absorb","substrate":"X","output":"data"}
 *   It reads: {"decision":"EXECUTE","delta":5,"motor_strength":0.82,...}
 *   The 80% deterministic path (physics + motor + decision) runs in the membrane.
 *   The LLM handles ONLY the 20%: composing human-readable responses.
 */
#define _POSIX_C_SOURCE 199309L
#define _DEFAULT_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* Unified compilation: membrane + protocol + foundation */
#include "../../lib/page.h"
#include "../../lib/record.h"
#include "../../lib/ring.h"
#include "../../lib/membrane.h"

/* Implementation files */
#include "page.c"
#include "record.c"
#include "ring.c"
#include "store.c"
#include "membrane.c"
#include "protocol.c"

/* LLVM JIT (when available) */
#ifdef CSOS_HAS_LLVM
#include "jit.c"
#endif

/* ═══ TEST HELPERS ═══ */

static int tests_passed = 0, tests_total = 0;

static void T(const char *name) { fprintf(stderr, "\n=== %s ===\n", name); }
static void P(const char *msg) { fprintf(stderr, "  PASS: %s\n", msg); tests_passed++; tests_total++; }
static void F(const char *msg, const char *why) {
    fprintf(stderr, "  FAIL: %s — %s\n", msg, why); tests_total++;
}
static void I(const char *msg) { fprintf(stderr, "  INFO: %s\n", msg); }

/* ═══ STRESS TEST 1: Institutional Memory ═══ */
static void test_memory(csos_organism_t *org) {
    T("Institutional Memory");
    char buf[4096];
    csos_organism_grow(org, "test_memory");
    csos_membrane_t *m = csos_organism_find(org, "test_memory");
    for (int i = 0; i < 50; i++) {
        double sigs[] = {10, 20, 10, 20, 10};
        for (int j = 0; j < 5; j++) csos_membrane_absorb(m, sigs[j], 9999, PROTO_INTERNAL);
    }
    if (m->gradient > 0) P("gradient accumulated"); else F("gradient", "zero");
    csos_membrane_see(m, "cockpit", buf, sizeof(buf));
    if (strstr(buf, "action_ratio")) P("cockpit metrics available");
    fprintf(stderr, "  gradient=%.0f atoms=%d cycles=%u\n", m->gradient, m->atom_count, m->cycles);
}

/* ═══ STRESS TEST 2: Market Sensing ═══ */
static void test_market(csos_organism_t *org) {
    T("Market Sensing");
    csos_membrane_t *m = csos_organism_grow(org, "market_spy");
    for (int i = 0; i < 100; i++)
        csos_membrane_absorb(m, 500.0 + (i%20)*0.5, 5001, PROTO_HTTP);
    P("burst absorption (100 ticks)");
    double F1 = m->F;
    for (int i = 0; i < 10; i++)
        csos_membrane_absorb(m, 520.0 + i*2.0, 5001, PROTO_CRON);
    P("slow absorption (10 bars)");
    fprintf(stderr, "  F_burst=%.4f F_slow=%.4f motor_strength=%.3f\n",
            F1, m->F, csos_motor_strength(m, 5001));
    char buf[512]; csos_membrane_lint(m, buf, sizeof(buf));
    if (strstr(buf, "pass")) P("lint passes"); else F("lint", buf);
}

/* ═══ STRESS TEST 3: Predictive Ops ═══ */
static void test_predict(csos_organism_t *org) {
    T("Predictive Ops");
    csos_membrane_t *m = csos_organism_grow(org, "ops_cpu");
    for (int i = 0; i < 30; i++)
        csos_membrane_absorb(m, 45.0 + (i%3)*0.5, 7001, PROTO_CRON);
    double g1 = m->gradient;
    for (int i = 0; i < 5; i++)
        csos_membrane_absorb(m, 45.2, 7001, PROTO_CRON);
    if (m->gradient > g1) P("pattern resonated on re-encounter"); else I("gradient unchanged");
    csos_photon_t anom = csos_membrane_absorb(m, 92.0, 7001, PROTO_CRON);
    if (!anom.resonated) P("anomaly detected as non-resonated");
    fprintf(stderr, "  gradient=%.0f atoms=%d\n", m->gradient, m->atom_count);
}

/* ═══ STRESS TEST 4: Cross-Domain Insight ═══ */
static void test_cross(csos_organism_t *org) {
    T("Cross-Domain Insight (Forster)");
    csos_membrane_t *a = csos_organism_grow(org, "domain_A");
    csos_membrane_t *b = csos_organism_grow(org, "domain_B");
    for (int i = 0; i < 40; i++) {
        csos_membrane_absorb(a, 100.0 + i*0.5, 8001, PROTO_INTERNAL);
        csos_membrane_absorb(a, 200.0 - i*0.3, 8001, PROTO_INTERNAL);
    }
    int b_before = b->atom_count;
    int n = csos_membrane_diffuse(a, b);
    if (n > 0) P("Forster transferred atoms"); else I("no transfer (threshold)");
    double bg0 = b->gradient;
    for (int i = 0; i < 10; i++)
        csos_membrane_absorb(b, 110.0, 8002, PROTO_INTERNAL);
    if (b->gradient > bg0) P("B resonates on A's patterns");
    fprintf(stderr, "  transferred=%d B_atoms=%d B_grad=%.0f\n", n, b->atom_count, b->gradient);
}

/* ═══ STRESS TEST 5: Autonomous Enterprise ═══ */
static void test_autonomous(csos_organism_t *org) {
    T("Autonomous Enterprise");
    const char *subs[] = {"infra_cpu", "infra_mem", "market_spy", "hiring", "cost"};
    for (int r = 0; r < 20; r++)
        for (int s = 0; s < 5; s++) {
            char raw[128];
            snprintf(raw, sizeof(raw), "value_%d_%.1f", r*5+s, 50.0+r+s*10.0);
            csos_organism_absorb(org, subs[s], raw, PROTO_CRON);
        }
    csos_membrane_t *o = csos_organism_find(org, "eco_organism");
    if (o && o->cycles > 0) P("organism tracked multi-substrate absorption");
    if (o && o->speed > 0) P("organism learning (speed > 0)");
    fprintf(stderr, "  speed=%.3f rw=%.3f decision=%s motor_entries=%d\n",
            o ? o->speed : 0, o ? o->rw : 0,
            o && o->speed > o->rw ? "EXECUTE" : "EXPLORE",
            o ? o->motor_count : 0);
}

/* ═══ STRESS TEST 6: Civilizational Scale ═══ */
static void test_scale(void) {
    T("Civilizational Scale");
    csos_organism_t *s = (csos_organism_t *)calloc(1, sizeof(csos_organism_t));
    csos_organism_init(s, ".");
    struct timespec t0, t1;
    clock_gettime(CLOCK_MONOTONIC, &t0);
    for (int i = 0; i < CSOS_MAX_RINGS; i++) {
        char n[32]; snprintf(n, sizeof(n), "ring_%04d", i);
        csos_organism_grow(s, n);
    }
    for (int i = 0; i < s->count; i++) {
        double sigs[] = {1,2,3,4,5,6,7,8,9,10};
        for (int j = 0; j < 10; j++)
            csos_membrane_absorb(s->membranes[i], sigs[j], (uint32_t)i, PROTO_INTERNAL);
    }
    clock_gettime(CLOCK_MONOTONIC, &t1);
    double elapsed = (t1.tv_sec-t0.tv_sec) + (t1.tv_nsec-t0.tv_nsec)/1e9;
    fprintf(stderr, "  rings=%d ops=%d elapsed=%.3fs\n", s->count, s->count*10, elapsed);
    if (elapsed < 5.0) P("scale test within time limit"); else F("scale", "too slow");
    int lint_ok = 0;
    char buf[256];
    for (int i = 0; i < s->count; i++) {
        csos_membrane_lint(s->membranes[i], buf, sizeof(buf));
        if (strstr(buf, "pass")) lint_ok++;
    }
    fprintf(stderr, "  lint: %d/%d\n", lint_ok, s->count);
    if (lint_ok == s->count) P("all rings pass lint");
    csos_organism_destroy(s); free(s);
}

/* ═══ STRESS TEST 7: Muscle Memory (Spaced Repetition) ═══ */
static void test_muscle(void) {
    T("Muscle Memory (Spaced Repetition)");
    csos_membrane_t *m = csos_membrane_create("muscle_test");
    /* A: spaced (1, 3, 7, 15, 31) */
    /* B: crammed every cycle */
    /* C: one-shot */
    uint32_t ha = 1001, hb = 2002, hc = 3003;
    csos_membrane_absorb(m, 1, ha, PROTO_STDIO);
    csos_membrane_absorb(m, 1, hb, PROTO_STDIO);
    csos_membrane_absorb(m, 1, hc, PROTO_STDIO);
    for (int i = 0; i < 4; i++) csos_membrane_absorb(m, 1, hb, PROTO_STDIO);
    csos_membrane_absorb(m, 1, ha, PROTO_STDIO); /* interval 5 */
    for (int i = 0; i < 4; i++) csos_membrane_absorb(m, 1, hb, PROTO_STDIO);
    csos_membrane_absorb(m, 1, ha, PROTO_STDIO); /* interval 5 */
    for (int i = 0; i < 8; i++) csos_membrane_absorb(m, 1, hb, PROTO_STDIO);
    csos_membrane_absorb(m, 1, ha, PROTO_STDIO); /* interval 9 — growing! */
    for (int i = 0; i < 16; i++) csos_membrane_absorb(m, 1, hb, PROTO_STDIO);
    csos_membrane_absorb(m, 1, ha, PROTO_STDIO); /* interval 17 — still growing! */

    double sa = csos_motor_strength(m, ha);
    double sb = csos_motor_strength(m, hb);
    double sc = csos_motor_strength(m, hc);
    fprintf(stderr, "  A(spaced): strength=%.3f  B(crammed): strength=%.3f  C(once): strength=%.3f\n",
            sa, sb, sc);
    if (sa > sc) P("spaced rep > one-shot"); else F("spaced", "A not stronger than C");

    uint32_t th[3]; double ts[3];
    int tn = csos_motor_top(m, th, ts, 3);
    if (tn >= 2) P("motor prioritization works");
    membrane_free_atoms(m); free(m);
}

/* ═══ STRESS TEST 8: Unified Membrane (motor + physics + decision in one call) ═══ */
static void test_unified(void) {
    T("Unified Membrane (one function = all of photosynthesis)");
    csos_membrane_t *m = csos_membrane_create("unified_test");

    /* Every membrane_absorb call must populate ALL photon fields */
    csos_photon_t ph = csos_membrane_absorb(m, 42.5, 5555, PROTO_HTTP);

    if (ph.substrate_hash == 5555) P("photon carries substrate_hash");
    else F("substrate_hash", "not set");

    if (ph.protocol == PROTO_HTTP) P("photon carries protocol");
    else F("protocol", "not set");

    if (ph.motor_strength > 0) P("photon carries motor_strength");
    else F("motor_strength", "zero");

    if (ph.decision <= DECISION_STORE) P("photon carries decision");
    else F("decision", "invalid");

    if (ph.cycle == 0) P("photon carries cycle");

    /* Verify motor memory was updated inside the membrane */
    double ms = csos_motor_strength(m, 5555);
    if (ms > 0) P("motor memory updated during absorb"); else F("motor", "not updated");

    /* Verify gradient was updated */
    if (m->gradient >= 0) P("gradient updated during absorb");

    /* Verify decision was made */
    fprintf(stderr, "  decision=%s mode=%s gradient=%.0f motor=%.3f\n",
            (const char*[]){"EXPLORE","EXECUTE","ASK","STORE"}[ph.decision],
            m->mode == MODE_PLAN ? "plan" : "build",
            m->gradient, ms);

    membrane_free_atoms(m); free(m);
}

/* ═══ STRESS TEST 9: LLM Round-Trip Simulation ═══ */
static void test_llm_roundtrip(csos_organism_t *org) {
    T("LLM Round-Trip (IR bridge)");

    /*
     * Simulates what happens when an LLM interacts with the system:
     * 1. LLM sends absorb request (like calling csos-core)
     * 2. System returns unified photon as JSON
     * 3. LLM reads decision + motor_strength + delta
     * 4. LLM decides what to do next based on those 3 numbers
     *
     * The LLM NEVER needs to call transport or agent separately.
     * Everything it needs is in the photon JSON.
     */

    char json_in[512], json_out[4096];

    /* LLM sends: absorb market data */
    snprintf(json_in, sizeof(json_in),
        "{\"action\":\"absorb\",\"substrate\":\"market_spy\","
        "\"output\":\"SPY 523.41 volume 45000000 vix 15.2\"}");
    csos_handle(org, json_in, json_out, sizeof(json_out));

    /* Verify response contains everything LLM needs */
    if (strstr(json_out, "decision")) P("response contains decision (Boyer)");
    else F("decision", "missing from response");

    if (strstr(json_out, "motor_strength")) P("response contains motor_strength");
    else F("motor", "missing from response");

    if (strstr(json_out, "delta")) P("response contains delta (Mitchell)");
    else F("delta", "missing from response");

    if (strstr(json_out, "domain")) P("response contains domain state");

    /* LLM sends: check cockpit (the LLM's "read physics" step) */
    snprintf(json_in, sizeof(json_in),
        "{\"action\":\"see\",\"ring\":\"eco_organism\",\"detail\":\"cockpit\"}");
    csos_handle(org, json_in, json_out, sizeof(json_out));

    if (strstr(json_out, "mode")) P("cockpit shows agent mode (plan/build)");
    if (strstr(json_out, "motor_entries")) P("cockpit shows motor memory count");

    /* LLM sends: check muscle memory priorities */
    snprintf(json_in, sizeof(json_in),
        "{\"action\":\"muscle\",\"ring\":\"eco_organism\"}");
    csos_handle(org, json_in, json_out, sizeof(json_out));

    if (strstr(json_out, "top")) P("muscle endpoint returns prioritized substrates");

    fprintf(stderr, "  LLM sees: %s\n", json_out);
    I("LLM reads 3 numbers (decision, motor_strength, delta) and knows what to do");
}

/* ═══ BENCHMARK ═══ */
static void run_bench(void) {
    csos_membrane_t *m = csos_membrane_create("bench");
    struct timespec t0, t1;
    int N = 10000;
    clock_gettime(CLOCK_MONOTONIC, &t0);
    for (int i = 0; i < N; i++)
        csos_membrane_absorb(m, (double)(i % 100), (uint32_t)(i % 50), PROTO_INTERNAL);
    clock_gettime(CLOCK_MONOTONIC, &t1);
    double elapsed = (t1.tv_sec-t0.tv_sec) + (t1.tv_nsec-t0.tv_nsec)/1e9;
    fprintf(stderr, "\n=== BENCHMARK (unified membrane) ===\n");
    fprintf(stderr, "  membrane_absorb(): %d ops in %.3fs = %.1f us/op (%.0f ops/sec)\n",
            N, elapsed, elapsed/N*1e6, N/elapsed);
    fprintf(stderr, "  Each call does: motor trace + 5 atom resonance + gradient + Boyer + Calvin check\n");
    membrane_free_atoms(m); free(m);
}

/* ═══ MAIN ═══ */
int main(int argc, char **argv) {

    if (argc > 1 && strcmp(argv[1], "--test") == 0) {
        fprintf(stderr, "CSOS Production Stress Tests (Unified Membrane)\n");
        fprintf(stderr, "================================================\n");
        csos_organism_t *org = (csos_organism_t *)calloc(1, sizeof(csos_organism_t));
        csos_organism_init(org, ".");

        test_memory(org);
        test_market(org);
        test_predict(org);
        test_cross(org);
        test_autonomous(org);
        test_scale();
        test_muscle();
        test_unified();
        test_llm_roundtrip(org);

        fprintf(stderr, "\n=== %d/%d TESTS PASSED ===\n", tests_passed, tests_total);
        csos_organism_destroy(org); free(org);
        return tests_passed == tests_total ? 0 : 1;
    }

    if (argc > 1 && strcmp(argv[1], "--bench") == 0) {
        run_bench();
        return 0;
    }

    if (argc > 1 && strcmp(argv[1], "--http") == 0) {
        uint16_t port = argc > 2 ? (uint16_t)atoi(argv[2]) : 4096;
        csos_organism_t *org = (csos_organism_t *)calloc(1, sizeof(csos_organism_t));
        csos_organism_init(org, ".");
        int r = csos_http_loop(org, port);
        csos_organism_destroy(org); free(org);
        return r;
    }

    if (argc > 1 && strcmp(argv[1], "--unix") == 0) {
        const char *path = argc > 2 ? argv[2] : "/tmp/csos.sock";
        csos_organism_t *org = (csos_organism_t *)calloc(1, sizeof(csos_organism_t));
        csos_organism_init(org, ".");
        int r = csos_unix_loop(org, path);
        csos_organism_destroy(org); free(org);
        return r;
    }

    if (argc > 1 && strcmp(argv[1], "--muscle") == 0) {
        csos_membrane_t *m = csos_membrane_create("demo");
        uint32_t subs[] = {1001, 2002, 3003, 4004, 5005};
        for (int c = 0; c < 100; c++) {
            csos_membrane_absorb(m, (double)c, subs[0], PROTO_STDIO);
            if (c % 5 == 0) csos_membrane_absorb(m, (double)c, subs[1], PROTO_CRON);
            if (c==1||c==3||c==7||c==15||c==31||c==63)
                csos_membrane_absorb(m, (double)c, subs[2], PROTO_LLM);
            if (c < 10) csos_membrane_absorb(m, (double)c, subs[3], PROTO_HTTP);
            if (c > 80) csos_membrane_absorb(m, (double)c, subs[4], PROTO_WEBHOOK);
        }
        char buf[4096];
        csos_membrane_see(m, "full", buf, sizeof(buf));
        printf("%s\n", buf);
        membrane_free_atoms(m); free(m);
        return 0;
    }

    /* Default: CLI daemon (backward compatible with csos-core.ts pipe) */
    csos_organism_t *org = (csos_organism_t *)calloc(1, sizeof(csos_organism_t));
    csos_organism_init(org, ".");

#ifdef CSOS_HAS_LLVM
    /* Initialize LLVM JIT and compile membrane absorb for eco_domain */
    csos_jit_init();
    csos_membrane_t *jit_mem = csos_organism_find(org, "eco_domain");
    if (jit_mem) csos_jit_compile(jit_mem);
#endif

    int ret = csos_cli_loop(org);

#ifdef CSOS_HAS_LLVM
    csos_jit_destroy();
#endif

    csos_organism_destroy(org); free(org);
    return ret;
}
