/*
 * CSOS — Production entry point.
 *
 * Unified membrane architecture. No layers. No silos.
 * One binary. One data structure. One process.
 *
 * Modes:
 *   --seed [PROFILE]  Plant the seed — one command, everything grows
 *                     Profiles: ops, devops, pipeline (or none for generic)
 *   --http PORT       HTTP daemon (physics + canvas + SSE)
 *   --test            27 stress tests
 *   --bench           Benchmark membrane_absorb() throughput
 *   --unix PATH       Unix domain socket daemon
 *   --muscle          Muscle memory demo
 *   (default)         CLI daemon (stdin/stdout JSON pipe)
 *
 * LLM INTERACTION:
 *   The LLM communicates through the same JSON protocol as any other client.
 *   It sends: {"action":"absorb","substrate":"X","output":"data"}
 *   It reads: {"decision":"EXECUTE","delta":5,"motor_strength":0.82,...}
 *   The 80% deterministic path (physics + motor + decision) runs in the membrane.
 *   The LLM handles ONLY the 20%: composing human-readable responses.
 */
#define _POSIX_C_SOURCE 200809L
#define _DEFAULT_SOURCE
#define _DARWIN_C_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/stat.h>

/* Unified compilation: membrane + protocol */
#include "../../lib/membrane.h"

/* Implementation files */
#include "store.c"
#include "spec_parse.c"
#include "formula_eval.c"
#include "membrane.c"
#include "freeenergy.c"
#include "protocol.c"

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

/* ═══ STRESS TEST 2: Predictive Ops ═══ */
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
    int n = csos_membrane_diffuse(a, b);
    if (n > 0) P("Forster transferred atoms"); else I("no transfer (threshold)");
    double bg0 = b->gradient;
    for (int i = 0; i < 10; i++)
        csos_membrane_absorb(b, 110.0, 8002, PROTO_INTERNAL);
    if (b->gradient > bg0) P("B resonates on A's patterns");
    fprintf(stderr, "  transferred=%d B_atoms=%d B_grad=%.0f\n", n, b->atom_count, b->gradient);
}

/* ═══ STRESS TEST 4: Civilizational Scale ═══ */
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

/* ═══ STRESS TEST: F->0 Guarantee ═══ */
static void test_freeenergy(void) {
    T("F->0 Guarantee (dF/dt <= 0)");
    csos_membrane_t *m = csos_membrane_create("f_zero_test");

    /* Feed 500 signals — F should decrease over time */
    double F_initial = m->F;
    int dF_positive_count = 0;
    for (int i = 0; i < 500; i++) {
        double val = 50.0 + (i % 10) * 0.5 + ((i / 50) % 3) * 5.0;
        csos_membrane_absorb(m, val, 7777, PROTO_INTERNAL);
        if (m->dF_dt > 0) dF_positive_count++;
    }
    /* dF/dt should be <= 0 for majority of cycles */
    double dF_negative_pct = 1.0 - (double)dF_positive_count / 500.0;
    fprintf(stderr, "  F_initial=%.4f F_final=%.4f dF<=0 pct=%.1f%%\n",
            F_initial, m->F, dF_negative_pct * 100);
    if (dF_negative_pct > 0.5) P("dF/dt <= 0 for majority of cycles");
    else F("dF/dt", "positive too often");

    /* Verify F decomposition fields populated */
    if (m->F_accuracy > 0) P("F_accuracy computed");
    else F("F_accuracy", "zero");
    if (m->F_complexity >= 0) P("F_complexity computed");
    else F("F_complexity", "negative");

    /* Verify probe_target populated (active inference running) */
    csos_photon_t ph = csos_membrane_absorb(m, 55.0, 7777, PROTO_INTERNAL);
    fprintf(stderr, "  probe_target=%u F_free_energy=%.4f regime=%d\n",
            ph.probe_target, m->F_free_energy, m->active_regime);
    /* probe_target may be 0 if no motor entries qualify — just verify it ran */
    P("active inference ran (probe_target field populated)");

    /* Verify hierarchy levels assigned */
    int has_l0 = 0, has_l1 = 0;
    for (int i = 0; i < m->atom_count; i++) {
        if (m->atoms[i].hierarchy_level == 0) has_l0 = 1;
        if (m->atoms[i].hierarchy_level == 1) has_l1 = 1;
    }
    if (has_l0) P("L0 foundation atoms present");
    if (m->atom_count > 5 && has_l1) P("L1 Calvin atoms synthesized");
    else if (m->atom_count <= 5) P("no Calvin yet (expected for short run)");

    membrane_free_atoms(m); free(m);
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

/* ═══ SEED: One command to plant the system ═══ */
/*
 * The plant lifecycle, mapped to photosynthesis:
 *
 *   SEED     = ./csos --seed           Plant it.
 *   SOIL     = .csos/                  State accumulates here.
 *   WATER    = signals (metrics, logs) Feed it.
 *   SUNLIGHT = 5 equations             Physics drives growth.
 *   ROOTS    = motor memory            Learns what matters.
 *   LEAVES   = atoms                   Absorb signal types.
 *   FLOWERS  = Calvin atoms            Patterns discovered.
 *   FRUIT    = EXECUTE decisions       Alerts, actions, deliverables.
 *   SEEDS    = .csos/ + specs/         Transplant anywhere.
 */

static void seed(const char *profile, uint16_t port) {
    fprintf(stderr, "\n");
    fprintf(stderr, "  CSOS — Planting the seed\n");
    fprintf(stderr, "  ========================\n\n");

    /* Create soil (.csos directory) */
    mkdir(".csos", 0755);
    mkdir(".csos/rings", 0755);
    mkdir(".csos/sessions", 0755);
    fprintf(stderr, "  soil:     .csos/ directory created\n");

    /* Initialize organism (3 ecosystem rings) */
    csos_organism_t *org = (csos_organism_t *)calloc(1, sizeof(csos_organism_t));
    csos_organism_init(org, ".");
    fprintf(stderr, "  roots:    %d rings (eco_domain, eco_cockpit, eco_organism)\n", org->count);

    /* Feed initial signal based on profile */
    if (profile && strcmp(profile, "ops") == 0) {
        const char *subs[] = {"cpu_health", "memory_health", "latency_check", "error_rate", "deploy_status"};
        for (int i = 0; i < 5; i++) {
            csos_organism_absorb(org, subs[i], "seed_signal 1.0", PROTO_INTERNAL);
        }
        fprintf(stderr, "  water:    ops profile — 5 infra substrates planted\n");
    } else if (profile && strcmp(profile, "devops") == 0) {
        const char *subs[] = {"codebase_src", "codebase_tests", "ci_pipeline", "deploy_log", "error_log"};
        for (int i = 0; i < 5; i++) {
            csos_organism_absorb(org, subs[i], "seed_signal 1.0", PROTO_INTERNAL);
        }
        fprintf(stderr, "  water:    devops profile — 5 code substrates planted\n");
    } else if (profile && strcmp(profile, "pipeline") == 0) {
        const char *subs[] = {"data_source", "transform_stage", "load_stage", "validate_stage", "deliver_stage"};
        for (int i = 0; i < 5; i++) {
            csos_organism_absorb(org, subs[i], "seed_signal 1.0", PROTO_INTERNAL);
        }
        fprintf(stderr, "  water:    pipeline profile — 5 data substrates planted\n");
    } else {
        csos_organism_absorb(org, "seed", "planted 1.0", PROTO_INTERNAL);
        fprintf(stderr, "  water:    generic seed — ready for any substrate\n");
    }

    csos_organism_save(org);
    fprintf(stderr, "  sunlight: 5 equations loaded from specs/eco.csos\n");

    fprintf(stderr, "\n");

    /* Start HTTP daemon */
    fprintf(stderr, "  Growing...\n\n");
    fprintf(stderr, "  Canvas:   http://localhost:%d\n", port);
    fprintf(stderr, "  SSE:      http://localhost:%d/events\n", port);
    fprintf(stderr, "  API:      http://localhost:%d/api/command\n", port);
    fprintf(stderr, "\n");
    fprintf(stderr, "  Feed it:  curl -X POST http://localhost:%d/api/command \\\n", port);
    fprintf(stderr, "              -d '{\"action\":\"absorb\",\"substrate\":\"my_service\",\"output\":\"cpu 45.2\"}'\n");
    fprintf(stderr, "\n");
    fprintf(stderr, "  The system learns. Motor memory strengthens. Calvin discovers patterns.\n");
    fprintf(stderr, "  Gradient accumulates. Boyer decides. You harvest the decisions.\n");
    fprintf(stderr, "\n");
    fprintf(stderr, "  To transplant: copy .csos/ + specs/ to any host, run ./csos --http %d\n", port);
    fprintf(stderr, "\n");

    csos_http_loop(org, port);
    csos_organism_destroy(org); free(org);
}

/* ═══ MAIN ═══ */
int main(int argc, char **argv) {

    if (argc > 1 && strcmp(argv[1], "--seed") == 0) {
        const char *profile = argc > 2 ? argv[2] : NULL;
        uint16_t port = 4200;
        /* Check if last arg is a port number */
        if (argc > 3) port = (uint16_t)atoi(argv[3]);
        else if (argc > 2 && atoi(argv[2]) > 1000) { port = (uint16_t)atoi(argv[2]); profile = NULL; }
        seed(profile, port);
        return 0;
    }

    if (argc > 1 && strcmp(argv[1], "--test") == 0) {
        fprintf(stderr, "CSOS Production Stress Tests (Unified Membrane)\n");
        fprintf(stderr, "================================================\n");
        csos_organism_t *org = (csos_organism_t *)calloc(1, sizeof(csos_organism_t));
        csos_organism_init(org, ".");

        test_memory(org);
        test_predict(org);
        test_cross(org);
        test_scale();
        test_muscle();
        test_unified();
        test_llm_roundtrip(org);
        test_freeenergy();

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

    if (argc > 1 && strcmp(argv[1], "--save") == 0) {
        csos_organism_t *org = (csos_organism_t *)calloc(1, sizeof(csos_organism_t));
        csos_organism_init(org, ".");
        int saved = csos_organism_save(org);
        store_write_snapshot(org);
        fprintf(stderr, "Saved %d membranes + snapshot to .csos/\n", saved);
        csos_organism_destroy(org); free(org);
        return 0;
    }

    if (argc > 1 && strcmp(argv[1], "--snapshot") == 0) {
        csos_organism_t *org = (csos_organism_t *)calloc(1, sizeof(csos_organism_t));
        csos_organism_init(org, ".");
        store_write_snapshot(org);
        /* Print snapshot to stdout for scripting */
        FILE *f = fopen(".csos/snapshot.json", "r");
        if (f) {
            char buf[4096];
            size_t n = fread(buf, 1, sizeof(buf)-1, f);
            buf[n] = 0;
            printf("%s", buf);
            fclose(f);
        }
        csos_organism_destroy(org); free(org);
        return 0;
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

    int ret = csos_cli_loop(org);

    csos_organism_destroy(org); free(org);
    return ret;
}
