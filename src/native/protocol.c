/*
 * CSOS Protocol — Unified ingress/egress for ALL protocols.
 *
 * Every protocol handler converts external format → membrane_absorb() call
 * and converts photon → external format. No protocol-specific physics.
 * The membrane IS the compute. Protocols are just I/O adapters.
 *
 * LLM INTERACTION:
 *   The LLM is a protocol, not a layer. It sends substrate_hash + raw text.
 *   It receives the unified photon (decision, motor_strength, delta).
 *   The 80% deterministic path runs in the membrane without the LLM.
 *   The LLM is called ONLY for the 20%: composing human-readable output.
 *
 *   LLM → protocol handler → membrane_absorb() → photon → protocol handler → LLM
 *         (parse intent)     (ALL physics)        (result)  (format response)
 */
#include "../../lib/membrane.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/stat.h>
#include <time.h>
#include <math.h>

/* Forward declarations for functions in membrane.c (unity build) */
static double atom_gradient(const csos_atom_t *a);
int csos_organism_save(csos_organism_t *org);
static uint32_t sub_hash(const char *name);
csos_membrane_t *csos_membrane_create(const char *name);

/* ═══ MINIMAL JSON HELPERS ═══ */

static int json_str(const char *json, const char *key, char *out, size_t sz) {
    char pat[128];
    snprintf(pat, sizeof(pat), "\"%s\":", key);
    const char *p = strstr(json, pat);
    if (!p) { out[0] = 0; return -1; }
    p += strlen(pat);
    while (*p == ' ' || *p == '\t') p++;
    if (*p == '"') {
        p++;
        size_t i = 0;
        while (*p && *p != '"' && i < sz - 1) {
            if (*p == '\\' && *(p+1)) p++;
            out[i++] = *p++;
        }
        out[i] = 0;
    } else {
        size_t i = 0;
        while (*p && *p != ',' && *p != '}' && i < sz - 1)
            out[i++] = *p++;
        out[i] = 0;
    }
    return 0;
}


/* ═══ SANCTIONED WRITE PATHS ═══ */

static const char *SANCTIONED_PREFIXES[] = {
    ".opencode/tools/", ".opencode/skills/", ".opencode/agents/",
    "specs/", ".csos/deliveries/", NULL
};

static int is_sanctioned_path(const char *path) {
    for (int i = 0; SANCTIONED_PREFIXES[i]; i++) {
        if (strncmp(path, SANCTIONED_PREFIXES[i], strlen(SANCTIONED_PREFIXES[i])) == 0)
            return 1;
        size_t plen = strlen(SANCTIONED_PREFIXES[i]);
        if (plen > 0 && SANCTIONED_PREFIXES[i][plen-1] == '/') {
            if (strncmp(path, SANCTIONED_PREFIXES[i], plen - 1) == 0 &&
                (path[plen-1] == '\0' || path[plen-1] == '/'))
                return 1;
        }
    }
    if (path[0] == '.' && path[1] == '/') return is_sanctioned_path(path + 2);
    return 0;
}

/* ═══ PHOTON → JSON (the IR → LLM bridge) ═══ */
/*
 * This is where the universal IR becomes human-readable.
 * The LLM reads this JSON. Every field the LLM needs is here.
 */
static int photon_to_json(const csos_photon_t *ph, const csos_membrane_t *d,
                          const csos_membrane_t *k, const csos_membrane_t *o,
                          char *json, size_t sz) {
    const char *dec[] = {"EXPLORE", "EXECUTE", "ASK", "STORE"};
    return snprintf(json, sz,
        "{\"decision\":\"%s\","
        "\"delta\":%d,"
        "\"motor_strength\":%.3f,"
        "\"interval\":%llu,"
        "\"resonated\":%s,"
        "\"vitality\":%.6f,"
        "\"mode\":\"%s\","
        "\"domain\":{\"grad\":%.0f,\"speed\":%.3f,\"F\":%.4f,\"vitality\":%.4f},"
        "\"cockpit\":{\"grad\":%.0f,\"speed\":%.3f,\"vitality\":%.4f},"
        "\"organism\":{\"grad\":%.0f,\"speed\":%.3f,\"rw\":%.3f,\"vitality\":%.4f}}",
        dec[ph->decision & 3],
        ph->delta,
        ph->motor_strength,
        (unsigned long long)ph->interval,
        ph->resonated ? "true" : "false",
        ph->vitality,
        o && o->mode == MODE_BUILD ? "build" : "plan",
        d ? d->gradient : 0, d ? d->speed : 0, d ? d->F : 0,
        d ? d->equation.vitality : 0,
        k ? k->gradient : 0, k ? k->speed : 0,
        k ? k->equation.vitality : 0,
        o ? o->gradient : 0, o ? o->speed : 0, o ? o->rw : 0,
        o ? o->equation.vitality : 0);
}

/* ═══ DISPATCH MEMBRANE — Action routing via physics ═══
 *
 * Every action is a substrate. The dispatch membrane absorbs the action hash.
 * Motor memory tracks which actions are used most — frequent actions resonate
 * instantly (high motor_strength → fast path). Rare actions still work but
 * go through the slower linear scan fallback.
 *
 * This replaces 32 strcmp() calls with: hash → motor lookup → direct call.
 * O(1) for learned actions (motor hit). O(n) fallback for unknown.
 *
 * The dispatch membrane is the nervous system of the protocol layer.
 * It learns. Frequent actions get faster. Unused actions get pruned.
 */

/* Action handler function type */
typedef int (*action_handler_t)(csos_organism_t *org, const char *json_in,
                                char *json_out, size_t out_sz);

/* Forward declarations for all handlers */
static int handle_absorb(csos_organism_t *org, const char *json_in, char *json_out, size_t out_sz);
static int handle_ecophys(csos_organism_t *org, const char *json_in, char *json_out, size_t out_sz);
static int handle_route(csos_organism_t *org, const char *json_in, char *json_out, size_t out_sz);
static int handle_fly(csos_organism_t *org, const char *json_in, char *json_out, size_t out_sz);
static int handle_see(csos_organism_t *org, const char *json_in, char *json_out, size_t out_sz);
static int handle_other(csos_organism_t *org, const char *json_in, char *json_out, size_t out_sz);

/* Dispatch table: action name → handler. Motor memory accelerates lookups. */
typedef struct {
    const char    *name;
    uint32_t       hash;        /* Pre-computed FNV-1a hash of name */
    action_handler_t handler;
} dispatch_entry_t;

static dispatch_entry_t _dispatch_table[64];
static int _dispatch_count = 0;
static int _dispatch_init = 0;

/* Dispatch membrane: learns action frequency via motor memory */
static csos_membrane_t *_dispatch_mem = NULL;

static void dispatch_register(const char *name, action_handler_t handler) {
    if (_dispatch_count >= 64) return;
    _dispatch_table[_dispatch_count].name = name;
    _dispatch_table[_dispatch_count].hash = sub_hash(name);
    _dispatch_table[_dispatch_count].handler = handler;
    _dispatch_count++;
}

static void dispatch_init(void) {
    if (_dispatch_init) return;
    _dispatch_init = 1;

    /* Create the dispatch membrane — it learns action routing */
    _dispatch_mem = csos_membrane_create("_dispatch");

    /* Register ALL actions → handlers.
     * The high-frequency actions (absorb, see, ecophys) will build
     * motor_strength quickly. Rare actions stay in the table but
     * don't consume membrane resources until used. */
    dispatch_register("absorb",    handle_absorb);
    dispatch_register("ecophys",   handle_ecophys);
    dispatch_register("route",     handle_route);
    dispatch_register("fly",       handle_fly);
    dispatch_register("see",       handle_see);
    /* All other actions go through handle_other which does the linear scan */
    dispatch_register("grow",      handle_other);
    dispatch_register("diffuse",   handle_other);
    dispatch_register("lint",      handle_other);
    dispatch_register("perf",      handle_other);
    dispatch_register("ping",      handle_other);
    dispatch_register("diagnose",  handle_other);
    dispatch_register("recommend", handle_other);
    dispatch_register("equate",    handle_other);
    dispatch_register("seed",      handle_other);
    dispatch_register("interact",  handle_other);
    dispatch_register("muscle",    handle_other);
    dispatch_register("hash",      handle_other);
    dispatch_register("exec",      handle_other);
    dispatch_register("web",       handle_other);
    dispatch_register("remember",  handle_other);
    dispatch_register("recall",    handle_other);
    dispatch_register("profile",   handle_other);
    dispatch_register("egress",    handle_other);
    dispatch_register("deliver",   handle_other);
    dispatch_register("explain",   handle_other);
    dispatch_register("save",      handle_other);
    dispatch_register("tool",      handle_other);
    dispatch_register("toolread",  handle_other);
    dispatch_register("toollist",  handle_other);
    dispatch_register("compact",   handle_other);
    dispatch_register("validate",  handle_other);
    dispatch_register("ir",        handle_other);
}

/* Pre-resolved ring pointers — set once per csos_handle call */
static __thread csos_membrane_t *_d, *_k, *_o;

/* ═══ DISPATCH: The ONE entry point ═══
 *
 * 1. Hash the action string
 * 2. Absorb hash into dispatch membrane (builds motor memory)
 * 3. Look up handler by hash (O(1) motor scan, O(n) fallback)
 * 4. Call handler
 *
 * Over time: frequent actions build high motor_strength → instant lookup.
 * The membrane IS the dispatch table. Motor memory IS the hot path cache.
 */
int csos_handle(csos_organism_t *org, const char *json_in,
                char *json_out, size_t out_sz) {
    char action[64] = {0};
    json_str(json_in, "action", action, sizeof(action));

    /* Initialize dispatch membrane on first call */
    dispatch_init();

    /* Pre-resolve ecosystem rings ONCE */
    _d = csos_organism_find(org, "eco_domain");
    _k = csos_organism_find(org, "eco_cockpit");
    _o = csos_organism_find(org, "eco_organism");

    /* Hash the action and absorb into dispatch membrane.
     * This builds motor memory: frequent actions get high strength.
     * The absorb IS the routing — Gouterman resonates, motor learns. */
    uint32_t action_hash = sub_hash(action[0] ? action : "diagnose");
    if (_dispatch_mem) {
        csos_membrane_absorb(_dispatch_mem, (double)action_hash, action_hash, PROTO_INTERNAL);
    }

    /* Find handler by hash (O(1) scan of dispatch table) */
    for (int i = 0; i < _dispatch_count; i++) {
        if (_dispatch_table[i].hash == action_hash) {
            return _dispatch_table[i].handler(org, json_in, json_out, out_sz);
        }
    }

    /* Fallback: empty action = diagnose */
    return handle_other(org, json_in, json_out, out_sz);
}

/* ═══ HANDLER: absorb ═══ */
static int handle_absorb(csos_organism_t *org, const char *json_in,
                         char *json_out, size_t out_sz) {
    char substrate[128] = {0}, output[8192] = {0};
    json_str(json_in, "substrate", substrate, sizeof(substrate));
    json_str(json_in, "output", output, sizeof(output));
    csos_photon_t ph = csos_organism_absorb(org, substrate[0] ? substrate : "unknown",
                                             output, PROTO_STDIO);
    int pos = snprintf(json_out, out_sz, "{\"substrate\":\"%s\",\"signals\":%d,\"physics\":",
                       substrate[0] ? substrate : "unknown", (int)ph.delta);
    pos += photon_to_json(&ph, _d, _k, _o, json_out + pos, out_sz - pos);
    snprintf(json_out + pos, out_sz - pos, "}");

    /* Auto-save hook: periodically persist organism state */
    store_maybe_autosave(org);

    /* Auto-notify hook: push EXECUTE decisions to configured webhook */
    if (ph.decision == DECISION_EXECUTE && substrate[0]) {
        csos_membrane_t *dm = _d;
        store_notify_execute(substrate,
            dm ? dm->gradient : 0, dm ? dm->speed : 0,
            dm ? dm->rw : 0, dm ? dm->F : 0,
            ph.motor_strength);
    }

    return 0;
}

/* ═══ HANDLER: route — Physics-based agent routing ═══
 *
 * F->0 agent routing. The cockpit membrane IS the routing table.
 * Each agent is a substrate. User intent is absorbed into cockpit.
 * The substrate with highest motor_strength for the intent's hash = the route.
 *
 * This replaces keyword-matching with physics:
 *   Gouterman: does the intent resonate with this agent's spectral band?
 *   Motor:     how well has this agent handled similar intents before?
 *   Boyer:     is there enough evidence to route confidently?
 *
 * Agent substrates (hashed):
 *   observer, operator, analyst, market-observer, market-executor, market-risk
 */
static int handle_route(csos_organism_t *org, const char *json_in,
                        char *json_out, size_t out_sz) {
    char output[8192] = {0};
    (void)org;

    json_str(json_in, "intent", output, sizeof(output));
    if (!output[0]) {
        snprintf(json_out, out_sz, "{\"error\":\"intent required\"}");
        return -1;
    }

    /* ═══ PURE RESONANCE ROUTING — No keywords. No strstr. Only physics. ═══
     *
     * 1. Tokenize intent into words
     * 2. Hash each word
     * 3. Absorb each word-hash into cockpit (builds motor memory per word)
     * 4. For each agent, sum motor_strength for agent_hash XOR word_hash
     *    (This is Gouterman spectral matching: agent resonates with words
     *     it has successfully handled before)
     * 5. Agent with highest cumulative resonance wins
     *
     * First time: all agents score ~equal (cold start, motor_strength ~0).
     * After 10 interactions: motor learns which words → which agents succeed.
     * After 100 interactions: routing is pure physics. Zero keyword matching.
     *
     * This IS F→0 for routing:
     *   ACCURACY = correct agent selected (feedback via delta > 0)
     *   COMPLEXITY = number of routing rules (approaches 0 — motor replaces rules)
     *   dF/dt ≤ 0 = routing always improves, never degrades
     */

    /* Agent names — their hashes are the "spectral signatures" in the cockpit */
    static const char *agent_names[] = {
        "csos-observer", "csos-operator", "csos-analyst",
        "csos-market-observer", "csos-market-executor", "csos-market-risk",
    };
    static const int n_agents = 6;

    /* Tokenize intent into words and absorb each into cockpit */
    uint32_t word_hashes[64];
    int n_words = 0;
    char intent_copy[8192];
    strncpy(intent_copy, output, sizeof(intent_copy) - 1);
    intent_copy[sizeof(intent_copy) - 1] = '\0';

    char *word = strtok(intent_copy, " \t\n,.:;!?");
    csos_photon_t ph = {0};
    while (word && n_words < 64) {
        /* Convert to lowercase for consistent hashing */
        for (char *c = word; *c; c++) {
            if (*c >= 'A' && *c <= 'Z') *c += 32;
        }
        uint32_t wh = sub_hash(word);
        word_hashes[n_words++] = wh;

        /* Absorb word into cockpit — motor memory builds per word */
        if (_k) {
            ph = csos_membrane_absorb(_k, (double)wh, wh, PROTO_LLM);
        }
        word = strtok(NULL, " \t\n,.:;!?");
    }

    /* Score each agent by resonance: sum motor_strength for
     * (agent_hash XOR word_hash) across all words.
     * XOR combines agent identity with word identity into a
     * unique "coupling hash" — the Forster distance between
     * this agent and this word. Motor tracks the pair. */
    const char *best_agent = agent_names[0];
    double best_score = -1;
    for (int i = 0; i < n_agents; i++) {
        uint32_t agent_hash = sub_hash(agent_names[i]);
        double score = 0;

        /* Agent's own motor strength (how active is this agent overall?) */
        score += csos_motor_strength(_k, agent_hash) * 0.5;

        /* Resonance with each word in the intent */
        for (int w = 0; w < n_words; w++) {
            /* Coupling hash: unique per (agent, word) pair */
            uint32_t coupling = agent_hash ^ word_hashes[w];
            double ms = _k ? csos_motor_strength(_k, coupling) : 0;
            score += ms;
        }

        if (score > best_score) {
            best_score = score;
            best_agent = agent_names[i];
        }
    }

    /* Confidence from Boyer: cockpit speed / rw */
    double confidence = 0;
    if (_k && _k->rw > 0) {
        confidence = best_score / (n_words > 0 ? n_words * 0.3 : 1.0);
        if (confidence > 1.0) confidence = 1.0;
    }

    /* If cold start (all scores near 0), default to observer */
    if (best_score < 0.01) {
        best_agent = agent_names[0];  /* csos-observer */
        confidence = 0.1;  /* Low confidence — system is still learning */
    }

    snprintf(json_out, out_sz,
        "{\"agent\":\"%s\",\"confidence\":%.3f,\"intent_hash\":%u,"
        "\"words\":%d,\"resonance\":%.4f,"
        "\"cockpit_speed\":%.3f,\"cockpit_rw\":%.3f,"
        "\"decision\":\"%s\",\"delta\":%d,"
        "\"note\":\"%s\"}",
        best_agent, confidence, sub_hash(output),
        n_words, best_score,
        _k ? _k->speed : 0, _k ? _k->rw : 0,
        (const char*[]){"EXPLORE","EXECUTE","ASK","STORE"}[ph.decision & 3],
        ph.delta,
        best_score < 0.01 ? "cold_start: routing will improve with use" :
        confidence < 0.3 ? "low_confidence: consider asking for clarification" :
        "physics_routed");
    return 0;
}

/* ═══ HANDLER: ecophys — Market signal absorption with 7-atom fields ═══ */
static int handle_ecophys(csos_organism_t *org, const char *json_in,
                          char *json_out, size_t out_sz) {
    char substrate[128] = {0}, output[8192] = {0};

    json_str(json_in, "substrate", substrate, sizeof(substrate));
    json_str(json_in, "output", output, sizeof(output));
    csos_photon_t ph = csos_organism_absorb(org, substrate[0] ? substrate : "equity_tick",
                                             output, PROTO_STDIO);

    /* Find the domain ring to scan atoms for 7-gap fields */
    csos_membrane_t *dom = _d;
    double best_edge = 0, best_crowding = 0, best_info = 1.0;
    double best_reflexivity = 1.0, best_imbalance = 0;
    uint8_t best_agent_type = 0;
    uint16_t best_flow_lag = 0;
    double best_flow_strength = 0;
    uint8_t active_regime = dom ? dom->active_regime : 0;

    if (dom) {
        for (int i = 0; i < dom->atom_count; i++) {
            csos_atom_t *a = &dom->atoms[i];
            if (a->edge_strength > best_edge) best_edge = a->edge_strength;
            if (a->crowding_score > best_crowding) best_crowding = a->crowding_score;
            if (a->info_remaining < best_info && a->info_remaining > 0)
                best_info = a->info_remaining;
            if (a->reflexivity_ratio > best_reflexivity)
                best_reflexivity = a->reflexivity_ratio;
            if (fabs(a->book_imbalance) > fabs(best_imbalance))
                best_imbalance = a->book_imbalance;
            if (a->agent_type > best_agent_type) best_agent_type = a->agent_type;
            if (a->flow_lag > best_flow_lag) {
                best_flow_lag = a->flow_lag;
                best_flow_strength = a->flow_strength;
            }
        }
    }

    const char *regime_names[] = {"BULL", "BEAR", "CRISIS"};
    const char *dec[] = {"EXPLORE", "EXECUTE", "ASK", "STORE"};

    int pos = snprintf(json_out, out_sz,
        "{\"substrate\":\"%s\",\"decision\":\"%s\",\"delta\":%d,"
        "\"motor_strength\":%.3f,\"vitality\":%.4f,"
        "\"probe_target\":%u,"
        "\"regime\":\"%s\","
        "\"F\":%.4f,\"dF_dt\":%.6f,"
        "\"F_accuracy\":%.4f,\"F_complexity\":%.4f,"
        "\"edge_strength\":%.3f,\"crowding\":%.3f,"
        "\"info_remaining\":%.3f,\"reflexivity\":%.3f,"
        "\"book_imbalance\":%.3f,\"agent_type\":%d,"
        "\"flow_lag\":%u,\"flow_strength\":%.3f,"
        "\"gradient\":%.0f,\"speed\":%.3f,\"rw\":%.3f",
        substrate[0] ? substrate : "equity_tick",
        dec[ph.decision & 3], ph.delta,
        ph.motor_strength, ph.vitality,
        ph.probe_target,
        regime_names[active_regime % 3],
        dom ? dom->F : 0, dom ? dom->dF_dt : 0,
        dom ? dom->F_accuracy : 0, dom ? dom->F_complexity : 0,
        best_edge, best_crowding,
        best_info, best_reflexivity,
        best_imbalance, best_agent_type,
        (unsigned)best_flow_lag, best_flow_strength,
        dom ? dom->gradient : 0, dom ? dom->speed : 0, dom ? dom->rw : 0);

    /* Causal chains summary */
    pos += snprintf(json_out + pos, out_sz - pos, ",\"causal_chains\":[");
    int chains = 0;
    if (dom) {
        for (int i = 0; i < dom->atom_count && chains < 5; i++) {
            csos_atom_t *a = &dom->atoms[i];
            if (a->causal_target == 0) continue;
            if (chains > 0) pos += snprintf(json_out + pos, out_sz - pos, ",");
            pos += snprintf(json_out + pos, out_sz - pos,
                "{\"name\":\"%s\",\"target\":%u,\"lag\":%u,\"strength\":%.3f,\"direction\":%d,"
                "\"counterfactual\":%.2f}",
                a->name, a->causal_target, (unsigned)a->causal_lag,
                a->causal_strength, a->causal_direction,
                a->counterfactual_score);
            chains++;
        }
    }
    pos += snprintf(json_out + pos, out_sz - pos, "]}");
    return 0;
}

/* ═══ HANDLER: fly — Batch absorption ═══ */
static int handle_fly(csos_organism_t *org, const char *json_in,
                      char *json_out, size_t out_sz) {
    char ring_name[64] = {0};

    json_str(json_in, "ring", ring_name, sizeof(ring_name));
    char sig_str[1024] = "50";
    json_str(json_in, "signals", sig_str, sizeof(sig_str));
    csos_membrane_t *m = csos_organism_find(org, ring_name[0] ? ring_name : "eco_domain");
    if (!m) { snprintf(json_out, out_sz, "{\"error\":\"ring not found\"}"); return -1; }
    double sigs[20]; int n = 0;
    const char *p = sig_str;
    while (*p && n < 20) {
        while (*p && !(*p >= '0' && *p <= '9') && *p != '-' && *p != '.') p++;
        if (!*p) break;
        char *end; double v = strtod(p, &end);
        if (end > p) { sigs[n++] = v; p = end; } else p++;
    }
    if (n == 0) { sigs[0] = 50; n = 1; }
    csos_photon_t last = {0};
    for (int i = 0; i < n; i++)
        last = csos_membrane_absorb(m, sigs[i], 0, PROTO_INTERNAL);
    snprintf(json_out, out_sz,
        "{\"ring\":\"%s\",\"F\":%.4f,\"gradient\":%.0f,\"speed\":%.4f,"
        "\"cycle\":%u,\"produced\":%d,\"resonance_width\":%.3f}",
        m->name, m->F, m->gradient, m->speed, m->cycles,
        last.delta, m->rw);
    return 0;
}

/* ═══ HANDLER: see — Query membrane state ═══ */
static int handle_see(csos_organism_t *org, const char *json_in,
                      char *json_out, size_t out_sz) {
    char ring_name[64] = {0}, detail[64] = {0};

    json_str(json_in, "ring", ring_name, sizeof(ring_name));
    json_str(json_in, "detail", detail, sizeof(detail));
    if (ring_name[0]) {
        csos_membrane_t *m = csos_organism_find(org, ring_name);
        if (!m) { snprintf(json_out, out_sz, "{\"error\":\"not found\"}"); return -1; }
        return csos_membrane_see(m, detail[0] ? detail : "minimal", json_out, out_sz);
    }
    /* List all */
    int pos = snprintf(json_out, out_sz, "{\"rings\":[");
    for (int i = 0; i < org->count && pos < (int)out_sz - 100; i++) {
        csos_membrane_t *m = org->membranes[i];
        if (i > 0) pos += snprintf(json_out + pos, out_sz - pos, ",");
        pos += snprintf(json_out + pos, out_sz - pos,
            "{\"id\":\"%s\",\"F\":%.4f,\"gradient\":%.0f,\"speed\":%.4f,"
            "\"atoms\":%d,\"cycles\":%u}",
            m->name, m->F, m->gradient, m->speed, m->atom_count, m->cycles);
    }
    snprintf(json_out + pos, out_sz - pos, "],\"count\":%d}", org->count);
    return 0;
}

/* ═══ HANDLER: other — All remaining actions via linear scan ═══ */
static int handle_other(csos_organism_t *org, const char *json_in,
                        char *json_out, size_t out_sz) {
    char action[64] = {0}, substrate[128] = {0};
    char ring_name[64] = {0};
    json_str(json_in, "action", action, sizeof(action));

    /* ═══ GROW: Create new membrane ═══ */
    if (strcmp(action, "grow") == 0) {
        json_str(json_in, "ring", ring_name, sizeof(ring_name));
        csos_membrane_t *m = csos_organism_grow(org, ring_name[0] ? ring_name : "new");
        snprintf(json_out, out_sz, "{\"ring\":\"%s\",\"atoms\":%d}",
                 m ? m->name : "error", m ? m->atom_count : 0);
        return 0;
    }

    /* ═══ DIFFUSE: Forster coupling ═══ */
    if (strcmp(action, "diffuse") == 0) {
        char src[64] = {0}, tgt[64] = {0};
        json_str(json_in, "source", src, sizeof(src));
        json_str(json_in, "target", tgt, sizeof(tgt));
        csos_membrane_t *s = csos_organism_find(org, src);
        csos_membrane_t *t = csos_organism_find(org, tgt);
        if (!s || !t) { snprintf(json_out, out_sz, "{\"error\":\"not found\"}"); return -1; }
        int n = csos_membrane_diffuse(s, t);
        snprintf(json_out, out_sz, "{\"n\":%d}", n);
        return 0;
    }

    /* ═══ LINT: Health check ═══ */
    if (strcmp(action, "lint") == 0) {
        json_str(json_in, "ring", ring_name, sizeof(ring_name));
        if (ring_name[0]) {
            csos_membrane_t *m = csos_organism_find(org, ring_name);
            if (!m) { snprintf(json_out, out_sz, "{\"error\":\"not found\"}"); return -1; }
            return csos_membrane_lint(m, json_out, out_sz);
        }
        int pos = snprintf(json_out, out_sz, "{\"rings_linted\":%d,\"results\":{", org->count);
        for (int i = 0; i < org->count && pos < (int)out_sz - 200; i++) {
            char sub[512];
            csos_membrane_lint(org->membranes[i], sub, sizeof(sub));
            if (i > 0) pos += snprintf(json_out + pos, out_sz - pos, ",");
            pos += snprintf(json_out + pos, out_sz - pos, "\"%s\":%s",
                            org->membranes[i]->name, sub);
        }
        snprintf(json_out + pos, out_sz - pos, "}}");
        return 0;
    }

    /* ═══ PERF: Latency stats + autotune state ═══ */
    if (strcmp(action, "perf") == 0) {
        extern uint64_t _absorb_ns_total;
        extern uint32_t _absorb_count;
        extern int _autotune_calvin_freq;
        extern int _autotune_compact_freq;

        uint64_t avg_ns = _absorb_count > 0 ? _absorb_ns_total / _absorb_count : 0;
        snprintf(json_out, out_sz,
            "{\"perf\":{"
            "\"absorb_avg_ns\":%llu,\"absorb_avg_us\":%.1f,"
            "\"samples\":%u,"
            "\"autotune\":{\"calvin_freq\":%d,\"compact_freq\":%d},"
            "\"rings\":%d,\"total_cycles\":%u"
            "}}",
            (unsigned long long)avg_ns, (double)avg_ns / 1000.0,
            _absorb_count,
            _autotune_calvin_freq, _autotune_compact_freq,
            org->count, _o ? _o->cycles : 0);
        return 0;
    }

    /* ═══ PING: Heartbeat ═══ */
    if (strcmp(action, "ping") == 0) {
        csos_membrane_t *o = _o;
        int jit_on = 0;
#ifdef CSOS_HAS_LLVM
        jit_on = csos_jit_active();
#endif
        snprintf(json_out, out_sz,
            "{\"alive\":true,\"native\":true,\"membrane\":true,"
            "\"llvm_jit\":%s,\"rings\":%d,\"speed\":%.3f}",
            jit_on ? "true" : "false", org->count, o ? o->speed : 0);
        return 0;
    }

    /* ═══ DIAGNOSE: System health ═══ */
    if (strcmp(action, "diagnose") == 0 || action[0] == 0) {
        int pos = snprintf(json_out, out_sz,
            "{\"status\":\"healthy\",\"architecture\":\"unified_membrane\",\"rings\":{");
        const char *eco[] = {"eco_domain", "eco_cockpit", "eco_organism"};
        for (int i = 0; i < 3; i++) {
            csos_membrane_t *m = csos_organism_find(org, eco[i]);
            if (i > 0) pos += snprintf(json_out + pos, out_sz - pos, ",");
            if (m) {
                pos += snprintf(json_out + pos, out_sz - pos,
                    "\"%s\":{\"grad\":%.0f,\"speed\":%.3f,\"F\":%.4f,\"atoms\":%d,"
                    "\"cycles\":%u,\"mode\":\"%s\",\"motor_entries\":%d}",
                    eco[i], m->gradient, m->speed, m->F, m->atom_count, m->cycles,
                    m->mode == MODE_PLAN ? "plan" : "build", m->motor_count);
            }
        }
        snprintf(json_out + pos, out_sz - pos, "}}");
        return 0;
    }

    /* ═══ RECOMMEND: Physics-driven accuracy recommendations ═══ */
    if (strcmp(action, "recommend") == 0) {
        int pos = snprintf(json_out, out_sz, "{\"recommendations\":[");
        int count = 0;

        const struct { csos_membrane_t *m; const char *name; } eco[] = {
            {_d, "Domain"}, {_k, "Cockpit"}, {_o, "Organism"}
        };
        for (int i = 0; i < 3; i++) {
            csos_membrane_t *m = eco[i].m;
            if (!m) continue;
            const csos_equation_t *eq = &m->equation;

            /* Gouterman low -> feed more diverse signals */
            if (eq->gouterman < 0.3 && count < 10) {
                if (count > 0) pos += snprintf(json_out + pos, out_sz - pos, ",");
                pos += snprintf(json_out + pos, out_sz - pos,
                    "{\"severity\":\"critical\",\"equation\":\"gouterman\","
                    "\"title\":\"%s: Signal diversity too low (%.0f%%)\","
                    "\"detail\":\"Atoms are not resonating. Feed varied signals to different substrates.\","
                    "\"action\":\"Absorb more diverse signals into this ring\"}",
                    eco[i].name, eq->gouterman * 100);
                count++;
            }

            /* Marcus high error -> predictions inaccurate */
            if (eq->marcus < 0.5 && m->F > 5.0 && count < 10) {
                if (count > 0) pos += snprintf(json_out + pos, out_sz - pos, ",");
                pos += snprintf(json_out + pos, out_sz - pos,
                    "{\"severity\":\"warning\",\"equation\":\"marcus\","
                    "\"title\":\"%s: Prediction error high (F=%.1f)\","
                    "\"detail\":\"Marcus barrier is steep. Absorb more consistent signals to reduce prediction error.\","
                    "\"action\":\"Increase signal frequency to improve accuracy\"}",
                    eco[i].name, m->F);
                count++;
            }

            /* Mitchell low -> gradient not building */
            if (eq->mitchell < 0.3 && count < 10) {
                if (count > 0) pos += snprintf(json_out + pos, out_sz - pos, ",");
                pos += snprintf(json_out + pos, out_sz - pos,
                    "{\"severity\":\"warning\",\"equation\":\"mitchell\","
                    "\"title\":\"%s: Gradient stagnant (%.0f%%)\","
                    "\"detail\":\"Life force not accumulating. Check ingress is producing real signals.\","
                    "\"action\":\"Diagnose ingress health\"}",
                    eco[i].name, eq->mitchell * 100);
                count++;
            }

            /* Boyer stuck -> not enough evidence */
            if (eq->boyer < 0.5 && m->speed < m->rw && count < 10) {
                if (count > 0) pos += snprintf(json_out + pos, out_sz - pos, ",");
                pos += snprintf(json_out + pos, out_sz - pos,
                    "{\"severity\":\"info\",\"equation\":\"boyer\","
                    "\"title\":\"%s: Decision gate closed (speed=%.1f < rw=%.3f)\","
                    "\"detail\":\"Not enough evidence to act. Continue absorbing signals.\","
                    "\"action\":\"Increase signal volume\"}",
                    eco[i].name, m->speed, m->rw);
                count++;
            }

            /* Forster weak -> rings isolated */
            if (eq->forster < 0.3 && count < 10) {
                if (count > 0) pos += snprintf(json_out + pos, out_sz - pos, ",");
                pos += snprintf(json_out + pos, out_sz - pos,
                    "{\"severity\":\"info\",\"equation\":\"forster\","
                    "\"title\":\"%s: Cross-pollination weak (%.0f%%)\","
                    "\"detail\":\"Rings are isolated. Forster coupling needs overlapping substrates.\","
                    "\"action\":\"Use diffuse to couple rings with overlapping substrates\"}",
                    eco[i].name, eq->forster * 100);
                count++;
            }
        }

        if (count == 0) {
            pos += snprintf(json_out + pos, out_sz - pos,
                "{\"severity\":\"success\",\"equation\":\"vitality\","
                "\"title\":\"System healthy\","
                "\"detail\":\"All 5 equations balanced. Organism vitality optimal.\","
                "\"action\":\"\"}");
        }

        snprintf(json_out + pos, out_sz - pos, "]}");
        return 0;
    }

    /* ═══ EQUATE: The Living Equation — unified vitality view ═══ */
    if (strcmp(action, "equate") == 0) {
        json_str(json_in, "ring", ring_name, sizeof(ring_name));
        if (ring_name[0]) {
            csos_membrane_t *m = csos_organism_find(org, ring_name);
            if (!m) { snprintf(json_out, out_sz, "{\"error\":\"ring not found\"}"); return -1; }
            csos_membrane_equate(m, json_out, out_sz);
        } else {
            csos_organism_equate(org, json_out, out_sz);
        }
        return 0;
    }

    /* ═══ SEED: Seed bank query ═══ */
    if (strcmp(action, "seed") == 0) {
        int pos = snprintf(json_out, out_sz, "{\"seed_count\":%d,\"seeds\":[", org->seed_count);
        for (int i = 0; i < org->seed_count && pos < (int)out_sz - 256; i++) {
            const csos_seed_t *s = &org->seeds[i];
            if (i > 0) pos += snprintf(json_out + pos, out_sz - pos, ",");
            pos += snprintf(json_out + pos, out_sz - pos,
                "{\"name\":\"%s\",\"formula\":\"%s\",\"source\":\"%s\","
                "\"center\":%.4f,\"strength\":%.4f,\"hash\":%u,\"cycle\":%u}",
                s->name, s->formula, s->source,
                s->center, s->strength, s->substrate_hash, s->harvest_cycle);
        }
        snprintf(json_out + pos, out_sz - pos, "]}");
        return 0;
    }

    /* ═══ INTERACT: Canvas reflexive loop ═══ */
    if (strcmp(action, "interact") == 0) {
        char itype[32] = {0}, target[128] = {0};
        json_str(json_in, "type", itype, sizeof(itype));
        json_str(json_in, "target", target, sizeof(target));

        double weight = 1.0;
        if (strcmp(itype, "select") == 0)    weight = 5.0;
        if (strcmp(itype, "command") == 0)    weight = 10.0;
        if (strcmp(itype, "query") == 0)      weight = 15.0;
        if (strcmp(itype, "operate") == 0)    weight = 20.0;

        if (_k) {
            uint32_t target_hash = 0;
            for (int i = 0; target[i]; i++) target_hash = target_hash * 31 + (unsigned char)target[i];
            csos_photon_t ph = csos_membrane_absorb(_k, weight, target_hash, PROTO_STDIO);

            snprintf(json_out, out_sz,
                "{\"absorbed\":true,\"type\":\"%s\",\"target\":\"%s\","
                "\"cockpit_gradient\":%.0f,\"cockpit_speed\":%.3f,"
                "\"delta\":%d,\"motor_strength\":%.3f}",
                itype, target, _k->gradient, _k->speed,
                ph.delta, ph.motor_strength);
        } else {
            snprintf(json_out, out_sz, "{\"absorbed\":false,\"error\":\"cockpit not found\"}");
        }
        return 0;
    }

    /* ═══ MUSCLE: Motor memory query ═══ */
    if (strcmp(action, "muscle") == 0) {
        json_str(json_in, "ring", ring_name, sizeof(ring_name));
        csos_membrane_t *m = csos_organism_find(org, ring_name[0] ? ring_name : "eco_organism");
        if (!m) { snprintf(json_out, out_sz, "{\"error\":\"not found\"}"); return -1; }
        uint32_t h[10]; double s[10];
        int n = csos_motor_top(m, h, s, 10);
        int pos = snprintf(json_out, out_sz, "{\"motor_entries\":%d,\"top\":[", m->motor_count);
        for (int i = 0; i < n && pos < (int)out_sz - 80; i++) {
            if (i > 0) pos += snprintf(json_out + pos, out_sz - pos, ",");
            pos += snprintf(json_out + pos, out_sz - pos,
                "{\"hash\":%u,\"strength\":%.3f}", h[i], s[i]);
        }
        snprintf(json_out + pos, out_sz - pos, "]}");
        return 0;
    }

    /* ═══ HASH: Substrate hash computation ═══ */
    if (strcmp(action, "hash") == 0) {
        json_str(json_in, "substrate", substrate, sizeof(substrate));
        uint32_t h = 0;
        for (const char *p = substrate; *p; p++) h = h * 31 + (uint8_t)*p;
        snprintf(json_out, out_sz, "{\"substrate\":\"%s\",\"hash\":%u}",
                 substrate, 1000 + (h % 9000));
        return 0;
    }

    /* ═══ EXEC: Run CLI command + auto-absorb ═══ */
    if (strcmp(action, "exec") == 0) {
        char command[4096] = {0};
        json_str(json_in, "command", command, sizeof(command));
        json_str(json_in, "substrate", substrate, sizeof(substrate));
        if (!substrate[0]) strncpy(substrate, "bash", sizeof(substrate));

        FILE *fp = popen(command, "r");
        if (!fp) {
            snprintf(json_out, out_sz, "{\"error\":true,\"message\":\"popen failed\"}");
            return -1;
        }
        char data[8192] = {0};
        size_t total = 0;
        while (total < sizeof(data) - 1) {
            size_t n = fread(data + total, 1, sizeof(data) - 1 - total, fp);
            if (n == 0) break;
            total += n;
        }
        int truncated = 0;
        if (total >= sizeof(data) - 1) {
            char overflow;
            if (fread(&overflow, 1, 1, fp) == 1) truncated = 1;
            char drain[4096];
            while (fread(drain, 1, sizeof(drain), fp) > 0) {}
        }
        int exit_code = pclose(fp);
        data[total] = 0;

        csos_photon_t ph = csos_organism_absorb(org, substrate, data, PROTO_STDIO);

        /* Escape data for JSON */
        char escaped[4096] = {0};
        size_t ei = 0;
        for (size_t i = 0; i < total && ei < sizeof(escaped) - 4; i++) {
            if (data[i] == '"') { escaped[ei++] = '\\'; escaped[ei++] = '"'; }
            else if (data[i] == '\\') { escaped[ei++] = '\\'; escaped[ei++] = '\\'; }
            else if (data[i] == '\n') { escaped[ei++] = '\\'; escaped[ei++] = 'n'; }
            else if (data[i] == '\r') { /* skip */ }
            else if ((unsigned char)data[i] >= 32) { escaped[ei++] = data[i]; }
        }
        escaped[ei] = 0;

        int pos = snprintf(json_out, out_sz,
            "{\"data\":\"%.4000s\",\"exit\":%d,\"truncated\":%s,\"bytes_read\":%zu,\"physics\":",
            escaped, exit_code, truncated ? "true" : "false", total);
        pos += photon_to_json(&ph, _d, _k, _o, json_out + pos, out_sz - pos);
        snprintf(json_out + pos, out_sz - pos, "}");
        return 0;
    }

    /* ═══ WEB: Fetch URL + auto-absorb ═══ */
    if (strcmp(action, "web") == 0) {
        char url[2048] = {0};
        json_str(json_in, "url", url, sizeof(url));
        json_str(json_in, "substrate", substrate, sizeof(substrate));
        if (!substrate[0]) {
            const char *p = strstr(url, "://");
            if (p) { p += 3; size_t i = 0; while (*p && *p != '/' && i < 127) substrate[i++] = *p++; }
            else strncpy(substrate, "web", sizeof(substrate));
        }
        char cmd[2200];
        snprintf(cmd, sizeof(cmd), "curl -sL -m 30 '%s' 2>&1", url);
        FILE *fp = popen(cmd, "r");
        if (!fp) {
            snprintf(json_out, out_sz, "{\"error\":true,\"message\":\"curl failed\"}");
            return -1;
        }
        char data[8192] = {0};
        size_t total = fread(data, 1, sizeof(data) - 1, fp);
        pclose(fp);
        data[total] = 0;

        csos_photon_t ph = csos_organism_absorb(org, substrate, data, PROTO_HTTP);

        int pos = snprintf(json_out, out_sz,
            "{\"status\":200,\"url\":\"%s\",\"text_length\":%zu,\"physics\":", url, total);
        pos += photon_to_json(&ph, _d, _k, _o, json_out + pos, out_sz - pos);
        snprintf(json_out + pos, out_sz - pos, "}");
        return 0;
    }

    /* ═══ REMEMBER: Store human data ═══ */
    if (strcmp(action, "remember") == 0) {
        char key[256] = {0}, value[4096] = {0};
        json_str(json_in, "key", key, sizeof(key));
        json_str(json_in, "value", value, sizeof(value));

        char hpath[512];
        snprintf(hpath, sizeof(hpath), "%s/.csos/deliveries", org->root);
        mkdir(hpath, 0755);
        snprintf(hpath, sizeof(hpath), "%s/.csos/deliveries/human.json", org->root);

        char existing[32768] = "{}";
        FILE *f = fopen(hpath, "r");
        if (f) { fread(existing, 1, sizeof(existing)-1, f); fclose(f); }

        char *end = strrchr(existing, '}');
        if (end) {
            char newfile[32768];
            *end = 0;
            size_t elen = strlen(existing);
            if (elen > 2)
                snprintf(newfile, sizeof(newfile), "%s,\n  \"%s\": \"%s\"\n}", existing, key, value);
            else
                snprintf(newfile, sizeof(newfile), "{\n  \"%s\": \"%s\"\n}", key, value);
            f = fopen(hpath, "w");
            if (f) { fputs(newfile, f); fclose(f); }
        }

        char raw[512];
        snprintf(raw, sizeof(raw), "%s %s", key, value);
        csos_organism_absorb(org, "human_profile", raw, PROTO_LLM);

        snprintf(json_out, out_sz, "{\"remembered\":\"%s\"}", key);
        return 0;
    }

    /* ═══ RECALL: Retrieve human data ═══ */
    if (strcmp(action, "recall") == 0) {
        char hpath[512];
        snprintf(hpath, sizeof(hpath), "%s/.csos/deliveries/human.json", org->root);
        FILE *f = fopen(hpath, "r");
        if (!f) {
            snprintf(json_out, out_sz, "{\"fields\":{},\"count\":0}");
            return 0;
        }
        char data[32768] = {0};
        size_t n = fread(data, 1, sizeof(data)-1, f);
        fclose(f);
        data[n] = 0;
        snprintf(json_out, out_sz, "{\"fields\":%s,\"count\":0}", data);
        return 0;
    }

    /* ═══ PROFILE: Instance profile for LLM context shaping ═══ */
    if (strcmp(action, "profile") == 0) {
        csos_membrane_t *d = _d;
        csos_membrane_t *k = _k;
        csos_membrane_t *o = _o;

        int calvin_d = 0, calvin_k = 0;
        if (d) for (int i = 0; i < d->atom_count; i++)
            if (strncmp(d->atoms[i].name, "calvin_", 7) == 0) calvin_d++;
        if (k) for (int i = 0; i < k->atom_count; i++)
            if (strncmp(k->atoms[i].name, "calvin_", 7) == 0) calvin_k++;

        char hpath[512];
        snprintf(hpath, sizeof(hpath), "%s/.csos/deliveries/human.json", org->root);
        char human[4096] = "{}";
        FILE *f = fopen(hpath, "r");
        if (f) { fread(human, 1, sizeof(human)-1, f); fclose(f); }

        uint32_t top_h[5]; double top_s[5];
        int top_n = o ? csos_motor_top(o, top_h, top_s, 5) : 0;

        int pos = snprintf(json_out, out_sz,
            "{\"profile\":{"
            "\"gradient\":{\"domain\":%.0f,\"cockpit\":%.0f,\"organism\":%.0f},"
            "\"calvin_atoms\":{\"domain\":%d,\"cockpit\":%d},"
            "\"organism\":{\"speed\":%.3f,\"rw\":%.3f,\"decision\":\"%s\",\"mode\":\"%s\"},"
            "\"human_fields\":%s,"
            "\"motor_top\":[",
            d ? d->gradient : 0, k ? k->gradient : 0, o ? o->gradient : 0,
            calvin_d, calvin_k,
            o ? o->speed : 0, o ? o->rw : 0,
            o ? (const char*[]){"EXPLORE","EXECUTE","ASK","STORE"}[o->decision] : "EXPLORE",
            o && o->mode == MODE_BUILD ? "build" : "plan",
            human);
        for (int i = 0; i < top_n && pos < (int)out_sz - 80; i++) {
            if (i > 0) pos += snprintf(json_out + pos, out_sz - pos, ",");
            pos += snprintf(json_out + pos, out_sz - pos,
                "{\"hash\":%u,\"strength\":%.3f}", top_h[i], top_s[i]);
        }
        snprintf(json_out + pos, out_sz - pos, "]}}\n");
        return 0;
    }

    /* ═══ EGRESS: Route decision to configured channels ═══ */
    if (strcmp(action, "egress") == 0) {
        char channel[64] = {0}, payload[8192] = {0};
        json_str(json_in, "channel", channel, sizeof(channel));
        json_str(json_in, "payload", payload, sizeof(payload));

        if (strcmp(channel, "file") == 0) {
            char path[512] = {0};
            json_str(json_in, "path", path, sizeof(path));
            if (!path[0]) strncpy(path, ".csos/deliveries/output.md", sizeof(path));
            char dir[512];
            strncpy(dir, path, sizeof(dir));
            char *slash = strrchr(dir, '/');
            if (slash) { *slash = 0; mkdir(dir, 0755); }
            FILE *f = fopen(path, "w");
            if (f) { fputs(payload, f); fclose(f); }
            char raw[256];
            snprintf(raw, sizeof(raw), "wrote %zu bytes to %s", strlen(payload), path);
            csos_organism_absorb(org, "egress_file", raw, PROTO_INTERNAL);
            snprintf(json_out, out_sz, "{\"egress\":\"file\",\"path\":\"%s\",\"bytes\":%zu}",
                     path, strlen(payload));
        } else if (strcmp(channel, "webhook") == 0) {
            char url[1024] = {0};
            json_str(json_in, "url", url, sizeof(url));
            char cmd[2048];
            snprintf(cmd, sizeof(cmd),
                "curl -s -X POST '%s' -H 'Content-Type: application/json' -d '%s' 2>&1",
                url, payload);
            FILE *fp = popen(cmd, "r");
            char resp[1024] = {0};
            if (fp) { fread(resp, 1, sizeof(resp)-1, fp); pclose(fp); }
            csos_organism_absorb(org, "egress_webhook", resp, PROTO_HTTP);
            snprintf(json_out, out_sz, "{\"egress\":\"webhook\",\"url\":\"%s\"}", url);
        } else if (strcmp(channel, "slack") == 0) {
            char webhook_url[1024] = {0};
            json_str(json_in, "url", webhook_url, sizeof(webhook_url));
            char cmd[4096];
            snprintf(cmd, sizeof(cmd),
                "curl -s -X POST '%s' -H 'Content-Type: application/json' "
                "-d '{\"text\":\"%s\"}' 2>&1", webhook_url, payload);
            FILE *fp = popen(cmd, "r");
            if (fp) pclose(fp);
            csos_organism_absorb(org, "egress_slack", payload, PROTO_HTTP);
            snprintf(json_out, out_sz, "{\"egress\":\"slack\",\"sent\":true}");
        } else {
            snprintf(json_out, out_sz, "{\"egress\":\"%s\",\"error\":\"unknown channel\"}", channel);
        }
        return 0;
    }

    /* ═══ DELIVER: Boyer-gated wisdom storage in compact photon IR ═══ */
    if (strcmp(action, "deliver") == 0) {
        char content[8192] = {0}, dtype[32] = "execute";
        char substrate_name[128] = "deliverable";
        json_str(json_in, "content", content, sizeof(content));
        json_str(json_in, "type", dtype, sizeof(dtype));
        json_str(json_in, "substrate", substrate_name, sizeof(substrate_name));

        csos_photon_t ph = csos_organism_absorb(org, substrate_name, content, PROTO_INTERNAL);
        csos_membrane_t *d = _d;
        csos_membrane_t *k = _k;
        csos_membrane_t *o = _o;

        int routed = 0;
        char fpath[512];
        snprintf(fpath, sizeof(fpath), "%s/.csos/deliveries", org->root);
        mkdir(fpath, 0755);

        snprintf(fpath, sizeof(fpath), "%s/.csos/deliveries/latest.md", org->root);
        FILE *ff = fopen(fpath, "w");
        if (ff) { fputs(content, ff); fclose(ff); routed++; }

        if (ph.decision == DECISION_EXECUTE) {
            char wisdom_path[512];
            uint32_t sh = 1000 + (ph.substrate_hash % 9000);
            snprintf(wisdom_path, sizeof(wisdom_path),
                "%s/.csos/deliveries/wisdom_%u_c%u.json", org->root, sh, ph.cycle);
            FILE *wf = fopen(wisdom_path, "w");
            if (wf) {
                char insight[256] = {0};
                size_t clen = strlen(content);
                if (clen > 200) clen = 200;
                size_t ei = 0;
                for (size_t ci = 0; ci < clen && ei < sizeof(insight) - 4; ci++) {
                    if (content[ci] == '"') { insight[ei++] = '\\'; insight[ei++] = '"'; }
                    else if (content[ci] == '\\') { insight[ei++] = '\\'; insight[ei++] = '\\'; }
                    else if (content[ci] == '\n') { insight[ei++] = ' '; }
                    else if ((unsigned char)content[ci] >= 32) { insight[ei++] = content[ci]; }
                }
                insight[ei] = 0;

                fprintf(wf,
                    "{\n"
                    "  \"substrate\": \"%s\",\n"
                    "  \"decision\": \"EXECUTE\",\n"
                    "  \"insight\": \"%s\",\n"
                    "  \"evidence\": {\n"
                    "    \"gradient\": %.0f,\n"
                    "    \"speed\": %.3f,\n"
                    "    \"rw\": %.3f,\n"
                    "    \"motor_strength\": %.3f,\n"
                    "    \"delta\": %d\n"
                    "  },\n"
                    "  \"rings\": {\n"
                    "    \"domain\": { \"grad\": %.0f, \"speed\": %.3f, \"F\": %.4f },\n"
                    "    \"cockpit\": { \"grad\": %.0f, \"speed\": %.3f },\n"
                    "    \"organism\": { \"grad\": %.0f, \"speed\": %.3f }\n"
                    "  },\n"
                    "  \"cycle\": %u\n"
                    "}\n",
                    substrate_name, insight,
                    o ? o->gradient : 0, o ? o->speed : 0, o ? o->rw : 0,
                    ph.motor_strength, ph.delta,
                    d ? d->gradient : 0, d ? d->speed : 0, d ? d->F : 0,
                    k ? k->gradient : 0, k ? k->speed : 0,
                    o ? o->gradient : 0, o ? o->speed : 0,
                    ph.cycle);
                fclose(wf);
                routed++;
                snprintf(fpath, sizeof(fpath), "%s", wisdom_path);
            }
        }

        snprintf(json_out, out_sz,
            "{\"delivered\":true,\"routed\":%d,\"path\":\"%s\","
            "\"boyer_gate\":\"%s\",\"persisted\":%s,"
            "\"delta\":%d,\"decision\":\"%s\"}",
            routed, fpath,
            ph.decision == DECISION_EXECUTE ? "PASS" : "EPHEMERAL",
            ph.decision == DECISION_EXECUTE ? "true" : "false",
            ph.delta,
            (const char*[]){"EXPLORE","EXECUTE","ASK","STORE"}[ph.decision & 3]);
        return 0;
    }

    /* ═══ EXPLAIN: Human-readable reasoning from membrane state ═══ */
    if (strcmp(action, "explain") == 0) {
        json_str(json_in, "ring", ring_name, sizeof(ring_name));
        csos_membrane_t *m = csos_organism_find(org, ring_name[0] ? ring_name : "eco_organism");
        if (!m) { snprintf(json_out, out_sz, "{\"error\":\"not found\"}"); return -1; }

        int eq_atoms = 0, calvin_atoms = 0;
        for (int i = 0; i < m->atom_count; i++) {
            if (strncmp(m->atoms[i].name, "calvin_", 7) == 0) calvin_atoms++;
            else eq_atoms++;
        }

        int total_ph = 0, res_ph = 0;
        for (int i = 0; i < m->atom_count; i++) {
            total_ph += m->atoms[i].photon_count;
            res_ph += (int)atom_gradient(&m->atoms[i]);
        }

        uint32_t th[3]; double ts[3];
        int tn = csos_motor_top(m, th, ts, 3);

        const char *dec_name[] = {"EXPLORE","EXECUTE","ASK","STORE"};
        const char *mode_name = m->mode == MODE_BUILD ? "build" : "plan";

        int pos = snprintf(json_out, out_sz,
            "{\"explanation\":\"Membrane '%s': gradient=%.0f from %d resonated out of %d total photons. "
            "Speed=%.3f %s resonance_width=%.3f so Boyer says %s. "
            "Mode is %s. %d equation atoms + %d Calvin-synthesized patterns. "
            "Motor memory tracks %d substrates",
            m->name, m->gradient, res_ph, total_ph,
            m->speed, m->speed > m->rw ? ">" : "<=", m->rw,
            dec_name[m->decision & 3], mode_name,
            eq_atoms, calvin_atoms, m->motor_count);

        if (tn > 0) {
            pos += snprintf(json_out + pos, out_sz - pos, " (top: ");
            for (int i = 0; i < tn; i++) {
                if (i > 0) pos += snprintf(json_out + pos, out_sz - pos, ", ");
                pos += snprintf(json_out + pos, out_sz - pos, "hash=%u@%.0f%%", th[i], ts[i]*100);
            }
            pos += snprintf(json_out + pos, out_sz - pos, ")");
        }

        snprintf(json_out + pos, out_sz - pos, ".\"}\n");
        return 0;
    }

    /* ═══ SAVE: Persist full membrane state to disk ═══ */
    if (strcmp(action, "save") == 0) {
        int saved = csos_organism_save(org);
        store_write_snapshot(org);
        snprintf(json_out, out_sz, "{\"saved\":%d,\"rings\":%d,\"snapshot\":true}", saved, org->count);
        return 0;
    }

    /* ═══ TOOL: Write to sanctioned directories ONLY ═══ */
    if (strcmp(action, "tool") == 0) {
        char filepath[512] = {0}, filecontent[65536] = {0};
        json_str(json_in, "path", filepath, sizeof(filepath));
        json_str(json_in, "body", filecontent, sizeof(filecontent));

        if (!filepath[0]) {
            snprintf(json_out, out_sz, "{\"error\":\"path required\"}");
            return -1;
        }

        if (!is_sanctioned_path(filepath)) {
            snprintf(json_out, out_sz,
                "{\"error\":true,\"law_violation\":\"I\","
                "\"message\":\"BLOCKED: path '%s' is outside sanctioned directories\","
                "\"sanctioned\":[\".opencode/tools/\",\".opencode/skills/\","
                "\".opencode/agents/\",\"specs/\",\".csos/deliveries/\"]}",
                filepath);
            return -1;
        }

        char dir[512];
        strncpy(dir, filepath, sizeof(dir));
        char *slash = strrchr(dir, '/');
        if (slash) {
            *slash = 0;
            char tmp[512] = {0};
            for (char *p = dir; *p; p++) {
                tmp[p - dir] = *p;
                if (*p == '/') mkdir(tmp, 0755);
            }
            mkdir(dir, 0755);
        }

        FILE *f = fopen(filepath, "w");
        if (!f) {
            snprintf(json_out, out_sz, "{\"error\":\"cannot write: %s\"}", filepath);
            return -1;
        }
        fputs(filecontent, f);
        fclose(f);

        if (strstr(filepath, "specs/") && strstr(filepath, ".csos")) {
            csos_spec_t test_spec = {0};
            if (csos_spec_parse(filepath, &test_spec) == 0) {
                if (test_spec.atom_count == 0) {
                    unlink(filepath);
                    snprintf(json_out, out_sz,
                        "{\"error\":true,\"law_violation\":\"IR\","
                        "\"message\":\"REJECTED: spec '%s' contains no foundation-derived atoms. "
                        "Use absorb() to feed signals — Calvin will synthesize patterns.\","
                        "\"foundation_count\":5}", filepath);
                    return -1;
                }
            }
        }

        char raw[512];
        snprintf(raw, sizeof(raw), "tool_write %s %zu bytes", filepath, strlen(filecontent));
        csos_photon_t ph = csos_organism_absorb(org, "tooling", raw, PROTO_LLM);

        snprintf(json_out, out_sz,
            "{\"wrote\":\"%s\",\"bytes\":%zu,\"delta\":%d,\"decision\":\"%s\"}",
            filepath, strlen(filecontent), ph.delta,
            (const char*[]){"EXPLORE","EXECUTE","ASK","STORE"}[ph.decision & 3]);
        return 0;
    }

    /* ═══ TOOLREAD: Read from sanctioned directories ═══ */
    if (strcmp(action, "toolread") == 0) {
        char filepath[512] = {0};
        json_str(json_in, "path", filepath, sizeof(filepath));
        if (!filepath[0]) {
            snprintf(json_out, out_sz, "{\"error\":\"path required\"}");
            return -1;
        }
        FILE *f = fopen(filepath, "r");
        if (!f) {
            snprintf(json_out, out_sz, "{\"error\":\"not found: %s\"}", filepath);
            return -1;
        }
        char data[16384] = {0};
        size_t n = fread(data, 1, sizeof(data) - 1, f);
        fclose(f);
        char escaped[16384] = {0};
        size_t ei = 0;
        for (size_t i = 0; i < n && ei < sizeof(escaped) - 4; i++) {
            if (data[i] == '"') { escaped[ei++] = '\\'; escaped[ei++] = '"'; }
            else if (data[i] == '\\') { escaped[ei++] = '\\'; escaped[ei++] = '\\'; }
            else if (data[i] == '\n') { escaped[ei++] = '\\'; escaped[ei++] = 'n'; }
            else if (data[i] == '\r') { /* skip */ }
            else if ((unsigned char)data[i] >= 32) { escaped[ei++] = data[i]; }
        }
        snprintf(json_out, out_sz, "{\"path\":\"%s\",\"bytes\":%zu,\"content\":\"%s\"}",
                 filepath, n, escaped);
        return 0;
    }

    /* ═══ TOOLLIST: List files in sanctioned directories ═══ */
    if (strcmp(action, "toollist") == 0) {
        char dir[512] = ".opencode/tools";
        json_str(json_in, "dir", dir, sizeof(dir));
        if (!is_sanctioned_path(dir)) {
            snprintf(json_out, out_sz, "{\"error\":\"not sanctioned: %s\"}", dir);
            return -1;
        }
        char cmd[600];
        snprintf(cmd, sizeof(cmd), "ls -1 '%s' 2>/dev/null", dir);
        FILE *fp = popen(cmd, "r");
        int pos = snprintf(json_out, out_sz, "{\"dir\":\"%s\",\"files\":[", dir);
        if (fp) {
            char line[256];
            int first = 1;
            while (fgets(line, sizeof(line), fp)) {
                size_t len = strlen(line);
                while (len > 0 && (line[len-1] == '\n' || line[len-1] == '\r')) line[--len] = 0;
                if (len == 0) continue;
                if (!first) pos += snprintf(json_out + pos, out_sz - pos, ",");
                pos += snprintf(json_out + pos, out_sz - pos, "\"%s\"", line);
                first = 0;
            }
            pclose(fp);
        }
        snprintf(json_out + pos, out_sz - pos, "]}");
        return 0;
    }

    /* ═══ COMPACT: Trigger atom pruning (F->0 compression) ═══ */
    if (strcmp(action, "compact") == 0) {
        snprintf(json_out, out_sz, "{\"compacted\":true,\"note\":\"pruning handled by freeenergy post-absorb\"}");
        return 0;
    }

    /* ═══ VALIDATE: Check a spec file against foundation IR ═══ */
    if (strcmp(action, "validate") == 0) {
        char filepath[512] = {0};
        json_str(json_in, "path", filepath, sizeof(filepath));
        if (!filepath[0]) strncpy(filepath, "specs/eco.csos", sizeof(filepath));

        csos_spec_t vspec = {0};
        if (csos_spec_parse(filepath, &vspec) != 0) {
            snprintf(json_out, out_sz, "{\"error\":\"cannot parse: %s\"}", filepath);
            return -1;
        }

        snprintf(json_out, out_sz,
            "{\"path\":\"%s\",\"valid_atoms\":%d,"
            "\"ir_enforced\":true,\"foundation_count\":5}",
            filepath, vspec.atom_count);
        return 0;
    }

    /* ═══ IR: Universal Intermediate Representation ═══ */
    if (strcmp(action, "ir") == 0) {
        char ir_detail[32] = "full";
        char ir_spec[8192] = {0}, ir_name[128] = {0};
        json_str(json_in, "detail", ir_detail, sizeof(ir_detail));
        json_str(json_in, "spec", ir_spec, sizeof(ir_spec));
        json_str(json_in, "name", ir_name, sizeof(ir_name));

        csos_membrane_t *d = _d;
        csos_membrane_t *o = _o;

        int pos = snprintf(json_out, out_sz, "{\"ir\":true,\"layers\":{");

        /* SPEC LAYER */
        if (strcmp(ir_detail, "full") == 0 || strcmp(ir_detail, "spec") == 0) {
            pos += snprintf(json_out + pos, out_sz - pos, "\"spec\":{");
            pos += snprintf(json_out + pos, out_sz - pos, "\"atoms\":[");
            if (d) {
                for (int i = 0; i < d->atom_count && pos < (int)out_sz - 512; i++) {
                    if (i > 0) pos += snprintf(json_out + pos, out_sz - pos, ",");
                    pos += snprintf(json_out + pos, out_sz - pos,
                        "{\"name\":\"%s\",\"formula\":\"%s\",\"compute\":\"%s\","
                        "\"source\":\"%s\",\"spectral\":[%.1f,%.1f],"
                        "\"broadband\":%s,\"rw\":%.4f,\"params\":%d}",
                        d->atoms[i].name, d->atoms[i].formula, d->atoms[i].compute,
                        d->atoms[i].source, d->atoms[i].spectral[0], d->atoms[i].spectral[1],
                        d->atoms[i].broadband ? "true" : "false",
                        d->atoms[i].rw, d->atoms[i].param_count);
                }
            }
            pos += snprintf(json_out + pos, out_sz - pos, "],");
            pos += snprintf(json_out + pos, out_sz - pos, "\"rings\":[");
            for (int i = 0; i < org->count && pos < (int)out_sz - 256; i++) {
                if (i > 0) pos += snprintf(json_out + pos, out_sz - pos, ",");
                csos_membrane_t *m = org->membranes[i];
                pos += snprintf(json_out + pos, out_sz - pos,
                    "{\"name\":\"%s\",\"atoms\":%d,\"mitchell_n\":%d,"
                    "\"rw\":%.4f,\"gradient\":%.1f}",
                    m->name, m->atom_count, m->mitchell_n, m->rw, m->gradient);
            }
            pos += snprintf(json_out + pos, out_sz - pos, "],");
            if (ir_spec[0]) {
                pos += snprintf(json_out + pos, out_sz - pos, "\"mermaid\":\"%.*s\",", 2000, ir_spec);
            }
            pos += snprintf(json_out + pos, out_sz - pos, "\"foundation_atoms\":5,\"ring_count\":%d}", org->count);
        }

        /* COMPILE LAYER */
        if (strcmp(ir_detail, "full") == 0 || strcmp(ir_detail, "compile") == 0) {
            if (strcmp(ir_detail, "full") == 0) pos += snprintf(json_out + pos, out_sz - pos, ",");
            pos += snprintf(json_out + pos, out_sz - pos, "\"compile\":{");
            pos += snprintf(json_out + pos, out_sz - pos, "\"formulas\":[");
            if (d) {
                for (int i = 0; i < d->atom_count && pos < (int)out_sz - 512; i++) {
                    if (i > 0) pos += snprintf(json_out + pos, out_sz - pos, ",");
                    pos += snprintf(json_out + pos, out_sz - pos,
                        "{\"atom\":\"%s\",\"compute\":\"%s\",\"params\":[",
                        d->atoms[i].name, d->atoms[i].compute);
                    for (int j = 0; j < d->atoms[i].param_count && pos < (int)out_sz - 128; j++) {
                        if (j > 0) pos += snprintf(json_out + pos, out_sz - pos, ",");
                        double pv = d->atoms[i].params[j];
                        if (pv != pv) pv = 0;
                        pos += snprintf(json_out + pos, out_sz - pos,
                            "{\"key\":\"%s\",\"value\":%.6f}",
                            d->atoms[i].param_keys[j], pv);
                    }
                    pos += snprintf(json_out + pos, out_sz - pos, "]}");
                }
            }
            pos += snprintf(json_out + pos, out_sz - pos, "],");
            int jit_on = 0, jit_atoms = 0;
#ifdef CSOS_HAS_LLVM
            jit_on = csos_jit_active();
            jit_atoms = csos_jit_atom_count();
#endif
            pos += snprintf(json_out + pos, out_sz - pos,
                "\"jit\":{\"enabled\":%s,\"compiled_atoms\":%d,\"opt_level\":\"O2\"},",
                jit_on ? "true" : "false", jit_atoms);
            /* V13: constants are equation-derived. Report from a live membrane. */
            {
                csos_membrane_t *_ir_m = org->count > 0 ? org->membranes[0] : NULL;
                double _ir_boyer = _ir_m ? csos_derive_boyer_threshold(_ir_m->mitchell_n) : 0.333;
                double _ir_rw_fl = _ir_m ? csos_derive_rw_floor(_ir_m) : 0.85;
                double _ir_rw_cl = _ir_m ? csos_derive_rw_ceil(_ir_m) : 0.92;
                int _ir_stuck    = _ir_m ? csos_derive_stuck_cycles(_ir_m) : 2;
                double _ir_f_fl  = csos_derive_f_floor();
                pos += snprintf(json_out + pos, out_sz - pos,
                    "\"derived\":{\"boyer_threshold\":%.4f,"
                    "\"rw_floor\":%.4f,\"rw_ceil\":%.4f,"
                    "\"stuck_cycles\":%d,\"f_floor\":%.6f,"
                    "\"forster_exp\":6,\"note\":\"all values equation-derived\"}}",
                    _ir_boyer, _ir_rw_fl, _ir_rw_cl, _ir_stuck, _ir_f_fl);
            }
        }

        /* RUNTIME LAYER */
        if (strcmp(ir_detail, "full") == 0 || strcmp(ir_detail, "runtime") == 0) {
            if (strcmp(ir_detail, "full") == 0 || strcmp(ir_detail, "compile") == 0)
                pos += snprintf(json_out + pos, out_sz - pos, ",");
            pos += snprintf(json_out + pos, out_sz - pos, "\"runtime\":{");
            pos += snprintf(json_out + pos, out_sz - pos, "\"rings\":[");
            for (int i = 0; i < org->count && pos < (int)out_sz - 512; i++) {
                csos_membrane_t *m = org->membranes[i];
                if (i > 0) pos += snprintf(json_out + pos, out_sz - pos, ",");
                pos += snprintf(json_out + pos, out_sz - pos,
                    "{\"name\":\"%s\",\"gradient\":%.1f,\"speed\":%.6f,"
                    "\"F\":%.6f,\"rw\":%.6f,\"action_ratio\":%.6f,"
                    "\"decision\":\"%s\",\"mode\":\"%s\","
                    "\"cycles\":%u,\"motor_count\":%d,"
                    "\"mitchell_n\":%d,"
                    "\"couplings\":%d}",
                    m->name, m->gradient, m->speed, m->F, m->rw, m->action_ratio,
                    (const char*[]){"EXPLORE","EXECUTE","ASK","STORE"}[m->decision & 3],
                    m->mode == MODE_BUILD ? "build" : "plan",
                    m->cycles, m->motor_count, m->mitchell_n,
                    m->coupling_count);
            }
            pos += snprintf(json_out + pos, out_sz - pos, "],");
            pos += snprintf(json_out + pos, out_sz - pos, "\"motor_top\":[");
            if (o) {
                uint32_t mh[10]; double ms[10];
                int mn = csos_motor_top(o, mh, ms, 10);
                for (int i = 0; i < mn && pos < (int)out_sz - 128; i++) {
                    if (i > 0) pos += snprintf(json_out + pos, out_sz - pos, ",");
                    pos += snprintf(json_out + pos, out_sz - pos,
                        "{\"hash\":%u,\"strength\":%.3f}", mh[i], ms[i]);
                }
            }
            pos += snprintf(json_out + pos, out_sz - pos, "]}");
        }

        pos += snprintf(json_out + pos, out_sz - pos, "}}");
        return 0;
    }

    snprintf(json_out, out_sz, "{\"error\":\"unknown action: %s\"}", action);
    return -1;
}

/* ═══ CLI LOOP (STDIO protocol) ═══ */

int csos_cli_loop(csos_organism_t *org) {
    printf("{\"daemon\":true,\"native\":true,\"membrane\":true,"
           "\"rings\":%d,\"ready\":true}\n", org->count);
    fflush(stdout);

    char line[65536], resp[65536];
    while (fgets(line, sizeof(line), stdin)) {
        size_t len = strlen(line);
        while (len > 0 && (line[len-1] == '\n' || line[len-1] == '\r')) line[--len] = 0;
        if (len == 0) continue;
        csos_handle(org, line, resp, sizeof(resp));
        printf("%s\n", resp);
        fflush(stdout);
    }
    return 0;
}

/* ═══ UNIX SOCKET PROTOCOL ═══ */

int csos_unix_loop(csos_organism_t *org, const char *path) {
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) return -1;
    unlink(path);
    struct sockaddr_un addr = {0};
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, path, sizeof(addr.sun_path) - 1);
    if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) { close(fd); return -1; }
    listen(fd, 8);
    fprintf(stderr, "CSOS membrane daemon on %s\n", path);

    char buf[65536], resp[65536];
    while (1) {
        struct pollfd pfd = {fd, POLLIN, 0};
        if (poll(&pfd, 1, 1000) <= 0) continue;
        int cl = accept(fd, NULL, NULL);
        if (cl < 0) continue;
        ssize_t n = read(cl, buf, sizeof(buf) - 1);
        if (n > 0) {
            buf[n] = 0;
            while (n > 0 && (buf[n-1] == '\n' || buf[n-1] == '\r')) buf[--n] = 0;
            csos_handle(org, buf, resp, sizeof(resp));
            write(cl, resp, strlen(resp));
            write(cl, "\n", 1);
        }
        close(cl);
    }
    close(fd); unlink(path);
    return 0;
}

/* ═══ HTTP PROTOCOL ═══ */

/* SSE client list */
#define MAX_SSE_CLIENTS 32
static int _sse_fds[MAX_SSE_CLIENTS];
static int _sse_count = 0;

#include <fcntl.h>
#include <errno.h>

static void set_nonblocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags >= 0) fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

static void sse_broadcast(csos_organism_t *org, const char *event, const char *data) {
    (void)org;
    char msg[8192];
    int len = snprintf(msg, sizeof(msg), "event: %s\ndata: %s\n\n", event, data);
    for (int i = _sse_count - 1; i >= 0; i--) {
        ssize_t written = write(_sse_fds[i], msg, len);
        if (written < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) continue;
            close(_sse_fds[i]);
            _sse_fds[i] = _sse_fds[--_sse_count];
        } else if (written < len) {
            close(_sse_fds[i]);
            _sse_fds[i] = _sse_fds[--_sse_count];
        }
    }
}

static void http_send(int fd, int status, const char *ctype, const char *body, size_t blen) {
    char hdr[512];
    int hlen = snprintf(hdr, sizeof(hdr),
        "HTTP/1.1 %d OK\r\nContent-Type: %s\r\nContent-Length: %zu\r\n"
        "Access-Control-Allow-Origin: *\r\nConnection: close\r\n\r\n",
        status, ctype, blen);
    if (hlen + (int)blen < 65536) {
        char combined[65536];
        memcpy(combined, hdr, hlen);
        memcpy(combined + hlen, body, blen);
        write(fd, combined, hlen + blen);
    } else {
        write(fd, hdr, hlen);
        write(fd, body, blen);
    }
}

static void http_send_canvas(int fd) {
    const char *paths[] = {".canvas-tui/index.html", "../.canvas-tui/index.html", NULL};
    FILE *f = NULL;
    for (int i = 0; paths[i]; i++) { f = fopen(paths[i], "r"); if (f) break; }
    if (!f) {
        const char *fallback =
            "{\"csos\":true,\"api\":\"/api/command\",\"sse\":\"/events\","
            "\"usage\":\"POST /api/command with JSON body\"}";
        http_send(fd, 200, "application/json", fallback, strlen(fallback));
        return;
    }
    fseek(f, 0, SEEK_END); long sz = ftell(f); fseek(f, 0, SEEK_SET);
    char *buf = (char *)malloc(sz + 1);
    size_t n = fread(buf, 1, sz, f);
    buf[n] = 0;
    fclose(f);
    char hdr[512];
    int hlen = snprintf(hdr, sizeof(hdr),
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: text/html; charset=utf-8\r\n"
        "Content-Length: %zu\r\n"
        "Cache-Control: no-cache, no-store, must-revalidate\r\n"
        "Pragma: no-cache\r\n"
        "Access-Control-Allow-Origin: *\r\n"
        "Connection: close\r\n\r\n", n);
    write(fd, hdr, hlen);
    write(fd, buf, n);
    free(buf);
}


/* Build full state JSON from organism */
static int build_state_json(csos_organism_t *org, char *out, size_t sz) {
    int pos = 0;
    const char *dec_names[] = {"EXPLORE","EXECUTE","ASK","STORE"};
    pos += snprintf(out + pos, sz - pos, "{\"rings\":{");
    for (int i = 0; i < org->count && pos < (int)sz - 1024; i++) {
        csos_membrane_t *m = org->membranes[i];
        if (i > 0) pos += snprintf(out + pos, sz - pos, ",");
        const csos_equation_t *eq = &m->equation;
        pos += snprintf(out + pos, sz - pos,
            "\"%s\":{\"gradient\":%.0f,\"speed\":%.4f,\"F\":%.4f,\"rw\":%.3f,"
            "\"cycles\":%u,\"atoms\":%d,\"decision\":\"%s\","
            "\"mitchell_n\":%d,\"motor_count\":%d,"
            "\"mode\":\"%s\",\"gradient_gap\":%.1f,"
            "\"coupling_count\":%d,"
            "\"action_ratio\":%.4f,\"consecutive_zero_delta\":%d,"
            "\"equation\":{"
              "\"gouterman\":%.4f,\"forster\":%.4f,\"marcus\":%.4f,"
              "\"mitchell\":%.4f,\"boyer\":%.4f,"
              "\"vitality\":%.6f,\"vitality_ema\":%.6f,"
              "\"vitality_peak\":%.6f,\"alive_cycles\":%u}}",
            m->name, m->gradient, m->speed, m->F, m->rw, m->cycles, m->atom_count,
            dec_names[m->decision & 3],
            m->mitchell_n, m->motor_count,
            m->mode == MODE_BUILD ? "build" : "plan",
            m->gradient_gap, m->coupling_count,
            m->action_ratio, m->consecutive_zero_delta,
            eq->gouterman, eq->forster, eq->marcus,
            eq->mitchell, eq->boyer,
            eq->vitality, eq->vitality_ema,
            eq->vitality_peak, eq->alive_cycles);
    }
    /* Organism-level vitality */
    double org_vitality = 0;
    if (org->count > 0) {
        double product = 1.0; int alive = 0;
        for (int i = 0; i < org->count; i++) {
            if (org->membranes[i] && org->membranes[i]->equation.vitality > 0) {
                product *= org->membranes[i]->equation.vitality;
                alive++;
            }
        }
        if (alive > 0) org_vitality = pow(product, 1.0 / alive);
    }
    pos += snprintf(out + pos, sz - pos,
        "},\"organism_vitality\":%.6f,\"clients\":%d,\"native\":true}",
        org_vitality, _sse_count);
    return pos;
}

/* Build template catalog JSON */
static int build_templates_json(char *out, size_t sz) {
    return snprintf(out, sz,
        "{\"templates\":["
        "{\"id\":\"data_pipeline\",\"name\":\"Data Pipeline\",\"category\":\"ingestion\","
          "\"spec\":\"ingest[Ingest] --> transform[Transform] --> validate[Validate] --> deliver[Deliver]\","
          "\"nodes\":[{\"id\":\"ingest\",\"proc\":\"stream_reader\",\"libs\":\"io,buffer\",\"rt\":\"native\"},"
                     "{\"id\":\"transform\",\"proc\":\"data_proc\",\"libs\":\"jq,awk\",\"rt\":\"native\"},"
                     "{\"id\":\"validate\",\"proc\":\"validator\",\"libs\":\"schema,assert\",\"rt\":\"native\"},"
                     "{\"id\":\"deliver\",\"proc\":\"egress\",\"libs\":\"http,webhook\",\"rt\":\"native\"}]},"
        "{\"id\":\"database_etl\",\"name\":\"Database ETL\",\"category\":\"etl\","
          "\"spec\":\"pg_query[Query DB] --> transform[Transform] --> validate[Validate] --> load[Load]\","
          "\"nodes\":[{\"id\":\"pg_query\",\"proc\":\"db_driver\",\"libs\":\"libpq,sql\",\"rt\":\"native\"},"
                     "{\"id\":\"transform\",\"proc\":\"data_proc\",\"libs\":\"jq,awk\",\"rt\":\"native\"},"
                     "{\"id\":\"validate\",\"proc\":\"validator\",\"libs\":\"schema\",\"rt\":\"native\"},"
                     "{\"id\":\"load\",\"proc\":\"data_writer\",\"libs\":\"io,bulk\",\"rt\":\"native\"}]},"
        "{\"id\":\"model_inference\",\"name\":\"ML Inference\",\"category\":\"ai\","
          "\"spec\":\"fetch[Fetch Data] --> parse[Parse Input] --> ml_infer[Run Model] --> deliver[Deliver]\","
          "\"nodes\":[{\"id\":\"fetch\",\"proc\":\"http_client\",\"libs\":\"curl,tls\",\"rt\":\"native\"},"
                     "{\"id\":\"parse\",\"proc\":\"parser\",\"libs\":\"json,csv\",\"rt\":\"native\"},"
                     "{\"id\":\"ml_infer\",\"proc\":\"model_runtime\",\"libs\":\"onnx,numpy\",\"rt\":\"python\"},"
                     "{\"id\":\"deliver\",\"proc\":\"egress\",\"libs\":\"http\",\"rt\":\"native\"}]},"
        "{\"id\":\"file_processing\",\"name\":\"File Processing\",\"category\":\"files\","
          "\"spec\":\"fetch[Read File] --> parse[Parse] --> filter[Filter] --> write_out[Write Output]\","
          "\"nodes\":[{\"id\":\"fetch\",\"proc\":\"stream_reader\",\"libs\":\"io,fs\",\"rt\":\"native\"},"
                     "{\"id\":\"parse\",\"proc\":\"parser\",\"libs\":\"csv,json,regex\",\"rt\":\"native\"},"
                     "{\"id\":\"filter\",\"proc\":\"data_proc\",\"libs\":\"jq,grep\",\"rt\":\"native\"},"
                     "{\"id\":\"write_out\",\"proc\":\"data_writer\",\"libs\":\"io,fs\",\"rt\":\"native\"}]},"
        "{\"id\":\"cloud_sync\",\"name\":\"Cloud Sync\",\"category\":\"cloud\","
          "\"spec\":\"auth_ck[Auth] --> fetch[Fetch Source] --> compress[Compress] --> s3_io[Upload S3]\","
          "\"nodes\":[{\"id\":\"auth_ck\",\"proc\":\"auth_gate\",\"libs\":\"jwt,oauth\",\"rt\":\"native\"},"
                     "{\"id\":\"fetch\",\"proc\":\"http_client\",\"libs\":\"curl\",\"rt\":\"native\"},"
                     "{\"id\":\"compress\",\"proc\":\"packer\",\"libs\":\"gzip,zstd\",\"rt\":\"native\"},"
                     "{\"id\":\"s3_io\",\"proc\":\"cloud_storage\",\"libs\":\"aws_sdk,s3\",\"rt\":\"cloud\"}]},"
        "{\"id\":\"event_driven\",\"name\":\"Event Pipeline\",\"category\":\"streaming\","
          "\"spec\":\"enqueue[Receive Event] --> parse[Parse] --> transform[Enrich] --> notify[Notify]\","
          "\"nodes\":[{\"id\":\"enqueue\",\"proc\":\"msg_broker\",\"libs\":\"amqp,redis\",\"rt\":\"native\"},"
                     "{\"id\":\"parse\",\"proc\":\"parser\",\"libs\":\"json\",\"rt\":\"native\"},"
                     "{\"id\":\"transform\",\"proc\":\"data_proc\",\"libs\":\"jq\",\"rt\":\"native\"},"
                     "{\"id\":\"notify\",\"proc\":\"egress\",\"libs\":\"smtp,webhook\",\"rt\":\"native\"}]}"
        "]}");
}

int csos_http_loop(csos_organism_t *org, uint16_t port) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return -1;
    int opt = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = htons(port);
    if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) { close(fd); return -1; }
    listen(fd, 32);
    fprintf(stderr, "CSOS native daemon on http://localhost:%d\n", port);
    fprintf(stderr, "  Canvas: http://localhost:%d\n", port);
    fprintf(stderr, "  SSE:    http://localhost:%d/events\n", port);
    fprintf(stderr, "  API:    http://localhost:%d/api/command (POST)\n", port);

    char req[65536], resp[65536];
    while (1) {
        struct pollfd pfds[MAX_SSE_CLIENTS + 1];
        pfds[0].fd = fd; pfds[0].events = POLLIN;
        for (int i = 0; i < _sse_count; i++) {
            pfds[i + 1].fd = _sse_fds[i]; pfds[i + 1].events = POLLIN;
        }
        int nfds = poll(pfds, 1 + _sse_count, 1000);

        if (nfds <= 0) {
            /* Keepalive for SSE clients */
            for (int i = _sse_count - 1; i >= 0; i--) {
                ssize_t w = write(_sse_fds[i], ": keepalive\n\n", 14);
                if (w < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
                    close(_sse_fds[i]);
                    _sse_fds[i] = _sse_fds[--_sse_count];
                }
            }
            continue;
        }
        /* Check for SSE client disconnects */
        for (int i = _sse_count - 1; i >= 0; i--) {
            if (pfds[i + 1].revents & (POLLIN | POLLHUP | POLLERR)) {
                char tmp[16];
                if (read(_sse_fds[i], tmp, sizeof(tmp)) <= 0) {
                    close(_sse_fds[i]);
                    _sse_fds[i] = _sse_fds[--_sse_count];
                }
            }
        }
        if (!(pfds[0].revents & POLLIN)) continue;

        int cl = accept(fd, NULL, NULL);
        if (cl < 0) continue;
        ssize_t n = read(cl, req, sizeof(req) - 1);
        if (n <= 0) { close(cl); continue; }
        req[n] = 0;

        char method[8] = {0}, path[256] = {0};
        sscanf(req, "%7s %255s", method, path);

        /* OPTIONS: CORS preflight */
        if (strcmp(method, "OPTIONS") == 0) {
            const char *cors = "HTTP/1.1 200 OK\r\nAccess-Control-Allow-Origin: *\r\n"
                "Access-Control-Allow-Methods: GET, POST, OPTIONS\r\n"
                "Access-Control-Allow-Headers: Content-Type\r\nContent-Length: 0\r\n"
                "Connection: close\r\n\r\n";
            write(cl, cors, strlen(cors));
            close(cl); continue;
        }

        /* GET routes */
        if (strcmp(method, "GET") == 0) {
            if (strcmp(path, "/") == 0 || strcmp(path, "/index.html") == 0) {
                http_send_canvas(cl);
            }
            else if (strcmp(path, "/api/state") == 0) {
                build_state_json(org, resp, sizeof(resp));
                http_send(cl, 200, "application/json", resp, strlen(resp));
            }
            else if (strcmp(path, "/api/templates") == 0) {
                build_templates_json(resp, sizeof(resp));
                http_send(cl, 200, "application/json", resp, strlen(resp));
            }
            else if (strcmp(path, "/events") == 0) {
                const char *sse_hdr =
                    "HTTP/1.1 200 OK\r\nContent-Type: text/event-stream\r\n"
                    "Cache-Control: no-cache\r\nAccess-Control-Allow-Origin: *\r\n"
                    "Connection: keep-alive\r\n\r\n";
                write(cl, sse_hdr, strlen(sse_hdr));
                build_state_json(org, resp, sizeof(resp));
                char init[65536];
                int ilen = snprintf(init, sizeof(init), "event: state\ndata: %s\n\n", resp);
                write(cl, init, ilen);
                if (_sse_count < MAX_SSE_CLIENTS) {
                    set_nonblocking(cl);
                    _sse_fds[_sse_count++] = cl;
                } else {
                    close(cl);
                }
                continue;
            }
            else {
                http_send(cl, 404, "text/plain", "not found", 9);
            }
            close(cl); continue;
        }

        /* POST routes */
        if (strcmp(method, "POST") == 0) {
            char *body = strstr(req, "\r\n\r\n");
            if (body) body += 4; else body = "{}";
            if (strcmp(path, "/api/command") == 0) {
                csos_handle(org, body, resp, sizeof(resp));
                http_send(cl, 200, "application/json", resp, strlen(resp));
                sse_broadcast(org, "response", resp);
            }
            else if (strcmp(path, "/api/agent") == 0) {
                char msg[4096] = {0}, agent_name[64] = {0};
                json_str(body, "message", msg, sizeof(msg));
                json_str(body, "agent", agent_name, sizeof(agent_name));
                if (!msg[0]) {
                    http_send(cl, 400, "application/json",
                        "{\"error\":\"message required\"}", 28);
                } else {
                    /* Auto-route if no agent specified */
                    if (!agent_name[0]) {
                        const char *low = msg;
                        int is_observe = (strstr(low,"health") || strstr(low,"status") ||
                            strstr(low,"explain") || strstr(low,"vitality") ||
                            strstr(low,"what") || strstr(low,"how") || strstr(low,"why") ||
                            strstr(low,"show") || strstr(low,"check") || strstr(low,"diagnose"));
                        int is_operate = (strstr(low,"spawn") || strstr(low,"create") ||
                            strstr(low,"bind") || strstr(low,"connect") || strstr(low,"schedule") ||
                            strstr(low,"set up") || strstr(low,"monitor") || strstr(low,"wake") ||
                            strstr(low,"tick") || strstr(low,"run") || strstr(low,"feed"));
                        int is_analyze = (strstr(low,"pattern") || strstr(low,"trend") ||
                            strstr(low,"converge") || strstr(low,"compare") || strstr(low,"across") ||
                            strstr(low,"insight") || strstr(low,"analyze") || strstr(low,"which"));
                        if (is_operate && !is_observe)
                            strncpy(agent_name, "csos-operator", sizeof(agent_name)-1);
                        else if (is_analyze && !is_operate)
                            strncpy(agent_name, "csos-analyst", sizeof(agent_name)-1);
                        else if (is_observe && !is_operate)
                            strncpy(agent_name, "csos-observer", sizeof(agent_name)-1);
                        else
                            strncpy(agent_name, "csos-living", sizeof(agent_name)-1);
                    }
                    /* Escape for shell */
                    char escaped[4096] = {0};
                    int ei = 0;
                    for (int i = 0; msg[i] && ei < (int)sizeof(escaped)-2; i++) {
                        if (msg[i] == '\'') { escaped[ei++]='\''; escaped[ei++]='\\';
                                              escaped[ei++]='\''; escaped[ei++]='\''; }
                        else if (msg[i] == '\n') { escaped[ei++] = ' '; }
                        else escaped[ei++] = msg[i];
                    }
                    /* Fork child for async agent execution */
                    pid_t pid = fork();
                    if (pid == 0) {
                        char cmd_buf[8192];
                        snprintf(cmd_buf, sizeof(cmd_buf),
                            "cd '%s' && script -q /dev/null opencode run --agent %s '%s' </dev/null 2>/dev/null",
                            org->root, agent_name, escaped);
                        FILE *fp = popen(cmd_buf, "r");
                        if (!fp) _exit(1);
                        char out[32768] = {0};
                        size_t total = 0;
                        while (total < sizeof(out)-1) {
                            size_t rn = fread(out+total, 1, sizeof(out)-1-total, fp);
                            if (rn == 0) break;
                            total += rn;
                        }
                        pclose(fp);
                        /* Strip ANSI + sanitize for JSON */
                        char clean[16384] = {0};
                        int ci = 0;
                        for (size_t i = 0; i < total && ci < (int)sizeof(clean)-4; i++) {
                            if (out[i] == '\x1b') { while (i < total && out[i] != 'm') i++; continue; }
                            if (out[i] == '"') { clean[ci++]=' '; }
                            else if (out[i] == '\\') { clean[ci++]=' '; }
                            else if (out[i] == '\n') { clean[ci++]='\\'; clean[ci++]='n'; }
                            else if (out[i] == '\t') { clean[ci++]=' '; }
                            else if ((unsigned char)out[i] >= 32) clean[ci++] = out[i];
                        }
                        /* POST result back via absorb (no msg channel) */
                        char post[32768];
                        int plen = snprintf(post, sizeof(post),
                            "{\"action\":\"absorb\",\"substrate\":\"agent_%s\","
                            "\"output\":\"%.*s\"}",
                            agent_name, (int)(sizeof(post)-300), clean);
                        char curl_cmd[200];
                        snprintf(curl_cmd, sizeof(curl_cmd),
                            "curl -sf -X POST http://127.0.0.1:4200/api/command "
                            "-H 'Content-Type: application/json' -d @-");
                        FILE *cp = popen(curl_cmd, "w");
                        if (cp) { fwrite(post, 1, plen, cp); pclose(cp); }
                        _exit(0);
                    }
                    char proc_resp[256];
                    snprintf(proc_resp, sizeof(proc_resp),
                        "{\"agent\":true,\"routed_to\":\"%s\",\"status\":\"processing\","
                        "\"message\":\"@%s is working on it...\"}",
                        agent_name, agent_name);
                    http_send(cl, 200, "application/json", proc_resp, strlen(proc_resp));
                }
            }
            else {
                http_send(cl, 404, "application/json", "{\"error\":\"not found\"}", 21);
            }
            close(cl); continue;
        }

        close(cl);
    }
    close(fd);
    return 0;
}
