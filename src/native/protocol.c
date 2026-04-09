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
#include <regex.h>
#include <sys/stat.h>
#include <time.h>

/* Forward declarations for JIT (defined in jit.c, included after protocol.c) */
#ifdef CSOS_HAS_LLVM
int csos_jit_active(void);
int csos_jit_atom_count(void);
#endif

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

/* ═══ NODE TYPE → DEFAULT COMMAND TEMPLATES ═══ */
/*
 * Maps known node types to sensible default command templates.
 * Actual commands come from configure, but this provides fallbacks.
 */
static const char* node_type_command(const char *node_id) {
    static const struct { const char *id; const char *cmd; } DEFAULTS[] = {
        {"fetch",     "curl -sf"},
        {"pg_query",  "psql -c"},
        {"mysql_q",   "mysql -e"},
        {"redis_op",  "redis-cli"},
        {"s3_io",     "aws s3"},
        {"gcs_io",    "gsutil"},
        {"validate",  "echo 'validate:'"},
        {"parse",     "jq '.'"},
        {"json_proc", "jq '.'"},
        {"csv_proc",  "awk -F,"},
        {"filter",    "jq"},
        {"compress",  "gzip"},
        {"encrypt",   "openssl enc"},
        {"log",       "logger"},
        {"monitor",   "curl -sf"},
        {"deploy",    "kubectl apply"},
        {NULL, NULL}
    };
    for (int i = 0; DEFAULTS[i].id; i++) {
        if (strcmp(node_id, DEFAULTS[i].id) == 0) return DEFAULTS[i].cmd;
    }
    return NULL;
}

/* ═══ LAW ENFORCEMENT ═══ */
/*
 * Law I revised: No code files OUTSIDE sanctioned directories.
 *
 * SANCTIONED (agents can write here via csos-core tool= action):
 *   .opencode/tools/*.ts      — OpenCode tool definitions
 *   .opencode/skills/**       — Skill definitions
 *   .opencode/agents/*.md     — Agent definitions
 *   specs/*.csos              — Substrate specifications
 *   .csos/deliveries/*        — Deliverables (any format)
 *
 * FORBIDDEN (code files created anywhere else):
 *   *.py, *.js, *.ts, *.jsx, *.tsx anywhere outside sanctioned paths
 */

/*
 * SANCTIONED write paths — agents can only write here.
 * .canvas-tui/ deliberately EXCLUDED: only index.html should exist,
 * and it's maintained by the system, not agents. This prevents
 * test file bloat from agent experimentation.
 */
static const char *SANCTIONED_PREFIXES[] = {
    ".opencode/tools/", ".opencode/skills/", ".opencode/agents/",
    "specs/", ".csos/deliveries/", ".csos/sessions/", NULL
};

static int is_sanctioned_path(const char *path) {
    for (int i = 0; SANCTIONED_PREFIXES[i]; i++) {
        if (strncmp(path, SANCTIONED_PREFIXES[i], strlen(SANCTIONED_PREFIXES[i])) == 0)
            return 1;
        /* Match without trailing slash: ".opencode/tools" matches ".opencode/tools/" */
        size_t plen = strlen(SANCTIONED_PREFIXES[i]);
        if (plen > 0 && SANCTIONED_PREFIXES[i][plen-1] == '/') {
            if (strncmp(path, SANCTIONED_PREFIXES[i], plen - 1) == 0 &&
                (path[plen-1] == '\0' || path[plen-1] == '/'))
                return 1;
        }
    }
    /* Also allow paths starting with ./ followed by sanctioned prefix */
    if (path[0] == '.' && path[1] == '/') return is_sanctioned_path(path + 2);
    return 0;
}

static const char *FORBIDDEN[] = {
    ">.*\\.(py|js|ts|jsx|tsx)", "cat.*>.*\\.(py|js|ts)",
    "tee.*\\.(py|js|ts)", "touch.*\\.(py|js|ts)", NULL
};

static int is_build_cmd(const char *cmd) {
    for (int i = 0; FORBIDDEN[i]; i++) {
        regex_t re;
        if (regcomp(&re, FORBIDDEN[i], REG_EXTENDED|REG_ICASE|REG_NOSUB) == 0) {
            int m = regexec(&re, cmd, 0, NULL, 0) == 0;
            regfree(&re);
            if (m) return 1;
        }
    }
    return 0;
}

/* ═══ PHOTON → JSON (the IR → LLM bridge) ═══ */
/*
 * This is where the universal IR becomes human-readable.
 * The LLM reads this JSON. Every field the LLM needs is here.
 * No second query needed. No separate transport lookup.
 * No separate agent state check. It's ALL in the photon.
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

/* ═══ REQUEST HANDLER (all protocols route here) ═══ */
/*
 * OPTIMIZATION: Pre-resolve eco_* ring pointers once per call.
 * Before: every action called csos_organism_find() 3-6 times (linear scan).
 * After: ONE set of lookups at entry, reused by all actions.
 *
 * Also: action dispatch uses first-char + length for O(1) routing
 * instead of sequential strcmp chain.
 */

/* Pre-resolved ring pointers — set once per csos_handle call */
static __thread csos_membrane_t *_d, *_k, *_o;

int csos_handle(csos_organism_t *org, const char *json_in,
                char *json_out, size_t out_sz) {
    char action[64] = {0}, substrate[128] = {0}, output[8192] = {0};
    char ring_name[64] = {0}, detail[32] = {0};
    json_str(json_in, "action", action, sizeof(action));

    /* Pre-resolve ecosystem rings ONCE (eliminates 15-30 find() calls per request) */
    _d = csos_organism_find(org, "eco_domain");
    _k = csos_organism_find(org, "eco_cockpit");
    _o = csos_organism_find(org, "eco_organism");

    if (strcmp(action, "absorb") == 0) {
        json_str(json_in, "substrate", substrate, sizeof(substrate));
        json_str(json_in, "output", output, sizeof(output));
        csos_photon_t ph = csos_organism_absorb(org, substrate[0] ? substrate : "unknown",
                                                 output, PROTO_STDIO);
        int pos = snprintf(json_out, out_sz, "{\"substrate\":\"%s\",\"signals\":%d,\"physics\":",
                           substrate[0] ? substrate : "unknown", (int)ph.delta);
        pos += photon_to_json(&ph, _d, _k, _o, json_out + pos, out_sz - pos);
        snprintf(json_out + pos, out_sz - pos, "}");
        return 0;
    }

    if (strcmp(action, "fly") == 0) {
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

    if (strcmp(action, "grow") == 0) {
        json_str(json_in, "ring", ring_name, sizeof(ring_name));
        csos_membrane_t *m = csos_organism_grow(org, ring_name[0] ? ring_name : "new");
        snprintf(json_out, out_sz, "{\"ring\":\"%s\",\"atoms\":%d}",
                 m ? m->name : "error", m ? m->atom_count : 0);
        return 0;
    }

    if (strcmp(action, "see") == 0) {
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
        /* Expose autotune state so agent/user can see self-tuning */
        extern uint64_t _absorb_ns_total;
        extern uint32_t _absorb_count;
        extern int _autotune_calvin_freq;
        extern int _autotune_compact_freq;

        uint64_t avg_ns = _absorb_count > 0 ? _absorb_ns_total / _absorb_count : 0;
        snprintf(json_out, out_sz,
            "{\"perf\":{"
            "\"absorb_avg_ns\":%llu,\"absorb_avg_us\":%.1f,"
            "\"samples\":%u,"
            "\"autotune\":{\"calvin_freq\":%d,\"compact_freq\":%d,\"target_us\":50},"
            "\"rings\":%d,\"total_cycles\":%u"
            "}}",
            (unsigned long long)avg_ns, (double)avg_ns / 1000.0,
            _absorb_count,
            _autotune_calvin_freq, _autotune_compact_freq,
            org->count, _o ? _o->cycles : 0);
        return 0;
    }

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

    /* ═══ RECOMMEND: Physics-driven accuracy recommendations ═══
     *
     * Canvas is READ-ONLY. It does not execute commands.
     * Instead it calls recommend to get physics-driven suggestions
     * that tell the agent what would make the system more accurate.
     *
     * Each recommendation is derived directly from the 5 equations:
     *   Gouterman → signal diversity (are we absorbing the right light?)
     *   Marcus → error correction (are predictions converging?)
     *   Mitchell → gradient health (is evidence accumulating?)
     *   Boyer → decision clarity (can we act with confidence?)
     *   Forster → cross-pollination (are sessions learning from each other?)
     */
    if (strcmp(action, "recommend") == 0) {
        int pos = snprintf(json_out, out_sz, "{\"recommendations\":[");
        int count = 0;

        /* Analyze each ecosystem ring */
        const struct { csos_membrane_t *m; const char *name; } eco[] = {
            {_d, "Domain"}, {_k, "Cockpit"}, {_o, "Organism"}
        };
        for (int i = 0; i < 3; i++) {
            csos_membrane_t *m = eco[i].m;
            if (!m) continue;
            const csos_equation_t *eq = &m->equation;

            /* Gouterman low → feed more diverse signals */
            if (eq->gouterman < 0.3 && count < 10) {
                if (count > 0) pos += snprintf(json_out + pos, out_sz - pos, ",");
                pos += snprintf(json_out + pos, out_sz - pos,
                    "{\"severity\":\"critical\",\"equation\":\"gouterman\","
                    "\"title\":\"%s: Signal diversity too low (%.0f%%)\","
                    "\"detail\":\"Atoms are not resonating. Feed varied signals to different substrates.\","
                    "\"action\":\"@csos-operator: bind sessions to diverse ingress sources\"}",
                    eco[i].name, eq->gouterman * 100);
                count++;
            }

            /* Marcus high error → predictions inaccurate */
            if (eq->marcus < 0.5 && m->F > 5.0 && count < 10) {
                if (count > 0) pos += snprintf(json_out + pos, out_sz - pos, ",");
                pos += snprintf(json_out + pos, out_sz - pos,
                    "{\"severity\":\"warning\",\"equation\":\"marcus\","
                    "\"title\":\"%s: Prediction error high (F=%.1f)\","
                    "\"detail\":\"Marcus barrier is steep. Absorb more consistent signals to reduce prediction error.\","
                    "\"action\":\"@csos-operator: increase tick frequency on active sessions\"}",
                    eco[i].name, m->F);
                count++;
            }

            /* Mitchell low → gradient not building */
            if (eq->mitchell < 0.3 && count < 10) {
                if (count > 0) pos += snprintf(json_out + pos, out_sz - pos, ",");
                pos += snprintf(json_out + pos, out_sz - pos,
                    "{\"severity\":\"warning\",\"equation\":\"mitchell\","
                    "\"title\":\"%s: Gradient stagnant (%.0f%%)\","
                    "\"detail\":\"Life force not accumulating. Check ingress is producing real signals.\","
                    "\"action\":\"@csos-observer: diagnose ingress health for bound sessions\"}",
                    eco[i].name, eq->mitchell * 100);
                count++;
            }

            /* Boyer stuck → not enough evidence */
            if (eq->boyer < 0.5 && m->speed < m->rw && count < 10) {
                if (count > 0) pos += snprintf(json_out + pos, out_sz - pos, ",");
                pos += snprintf(json_out + pos, out_sz - pos,
                    "{\"severity\":\"info\",\"equation\":\"boyer\","
                    "\"title\":\"%s: Decision gate closed (speed=%.1f < rw=%.3f)\","
                    "\"detail\":\"Not enough evidence to act. Continue absorbing signals.\","
                    "\"action\":\"@csos-operator: tick sessions or increase signal volume\"}",
                    eco[i].name, m->speed, m->rw);
                count++;
            }

            /* Forster weak → sessions isolated */
            if (eq->forster < 0.3 && count < 10) {
                if (count > 0) pos += snprintf(json_out + pos, out_sz - pos, ",");
                pos += snprintf(json_out + pos, out_sz - pos,
                    "{\"severity\":\"info\",\"equation\":\"forster\","
                    "\"title\":\"%s: Cross-pollination weak (%.0f%%)\","
                    "\"detail\":\"Sessions are isolated. Forster coupling needs overlapping substrates.\","
                    "\"action\":\"@csos-analyst: check convergence and recommend merges\"}",
                    eco[i].name, eq->forster * 100);
                count++;
            }
        }

        /* Session-level recommendations */
        for (int i = 0; i < org->session_count && count < 10; i++) {
            const csos_session_t *s = &org->sessions[i];

            /* Unbound sessions */
            if (!s->ingress.type[0] && s->stage <= SESSION_SEED) {
                if (count > 0) pos += snprintf(json_out + pos, out_sz - pos, ",");
                pos += snprintf(json_out + pos, out_sz - pos,
                    "{\"severity\":\"warning\",\"equation\":\"session\","
                    "\"title\":\"Session '%s' has no ingress\","
                    "\"detail\":\"Living equation is unbound. It cannot absorb signals from the outside world.\","
                    "\"action\":\"@csos-operator: session=bind id=%s with ingress_type and ingress_source\"}",
                    s->id, s->id);
                count++;
            }

            /* Stuck dormant with schedule */
            if (s->stage == SESSION_DORMANT && s->schedule.autonomous &&
                s->vitality < 0.1 && count < 10) {
                if (count > 0) pos += snprintf(json_out + pos, out_sz - pos, ",");
                pos += snprintf(json_out + pos, out_sz - pos,
                    "{\"severity\":\"warning\",\"equation\":\"session\","
                    "\"title\":\"Session '%s' dormant with zero vitality\","
                    "\"detail\":\"Scheduled but not producing. Ingress may be failing or returning empty data.\","
                    "\"action\":\"@csos-observer: session=observe id=%s to diagnose\"}",
                    s->id, s->id);
                count++;
            }

            /* Bloom ready for harvest */
            if (s->stage == SESSION_BLOOM && count < 10) {
                if (count > 0) pos += snprintf(json_out + pos, out_sz - pos, ",");
                pos += snprintf(json_out + pos, out_sz - pos,
                    "{\"severity\":\"success\",\"equation\":\"boyer\","
                    "\"title\":\"Session '%s' ready to deliver (BLOOM)\","
                    "\"detail\":\"Boyer says EXECUTE. Enough evidence accumulated. Harvest the results.\","
                    "\"action\":\"@csos-operator: deliver from %s or let egress fire automatically\"}",
                    s->id, s->id);
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

    /* ═══ EVENTS: Shared log between agent and canvas ═══ */
    if (strcmp(action, "events") == 0) {
        char sub[32] = {0}, msg[256] = {0}, sess[64] = {0};
        json_str(json_in, "sub", sub, sizeof(sub));
        if (strcmp(sub, "log") == 0) {
            /* Agent or Canvas logging an event */
            char src[32] = {0};
            json_str(json_in, "source", src, sizeof(src));
            json_str(json_in, "session", sess, sizeof(sess));
            json_str(json_in, "message", msg, sizeof(msg));
            csos_event_log(org, EVT_AGENT_ACTION,
                           src[0] ? src : "agent", sess, msg, 0);
            snprintf(json_out, out_sz, "{\"logged\":true}");
        } else {
            /* Default: list recent events */
            csos_event_list(org, json_out, out_sz);
        }
        return 0;
    }

    /* ═══ INTERACT: Canvas reflexive loop ═══
     *
     * In photosynthesis, light-harvesting complexes don't just absorb sunlight —
     * they absorb the ATTENTION of the photon. Where light falls, energy flows.
     *
     * Canvas user attention is the same: when the human clicks a ring, selects a
     * session, hovers over an equation, or types a query — that IS a signal.
     * It absorbs into eco_cockpit as "human attention photons".
     *
     * This creates the REFLEXIVE LOOP:
     *   Canvas interaction → eco_cockpit absorption → cockpit gradient shifts
     *   → agents read cockpit → agents know what matters to the human
     *   → agent actions → SSE → canvas updates
     *
     * No separate "focus" channel. No polling. The gradient IS the communication.
     */
    if (strcmp(action, "interact") == 0) {
        char itype[32] = {0}, target[128] = {0}, sess_id[CSOS_NAME_LEN] = {0};
        json_str(json_in, "type", itype, sizeof(itype));
        json_str(json_in, "target", target, sizeof(target));
        json_str(json_in, "session", sess_id, sizeof(sess_id));

        /* Compute attention signal: type determines weight */
        double weight = 1.0;  /* default: passive attention (hover, view) */
        if (strcmp(itype, "select") == 0)    weight = 5.0;   /* focused attention */
        if (strcmp(itype, "command") == 0)    weight = 10.0;  /* active engagement */
        if (strcmp(itype, "query") == 0)      weight = 15.0;  /* deliberate inquiry */
        if (strcmp(itype, "operate") == 0)    weight = 20.0;  /* direct action */

        /* Absorb into cockpit — the human attention ring */
        if (_k) {
            uint32_t target_hash = 0;
            for (int i = 0; target[i]; i++) target_hash = target_hash * 31 + (unsigned char)target[i];
            csos_photon_t ph = csos_membrane_absorb(_k, weight, target_hash, PROTO_STDIO);

            /* If session context, also absorb into that session's substrate */
            if (sess_id[0]) {
                csos_session_t *s = csos_session_find(org, sess_id);
                if (s && _d) {
                    csos_membrane_absorb(_d, weight * 0.5, s->substrate_hash, PROTO_STDIO);
                }
            }

            /* Log as canvas event */
            char emsg[CSOS_EVENT_MSG_LEN];
            snprintf(emsg, sizeof(emsg), "canvas:%s target=%s", itype, target);
            csos_event_log(org, EVT_CANVAS_ACTION, "canvas", sess_id, emsg, 0);

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

    /* ═══ MSG: Agent ↔ Canvas message channel ═══ */
    if (strcmp(action, "msg") == 0) {
        char sub[32] = {0}, from[32] = {0}, to[32] = {0};
        char body[1024] = {0}, sess[64] = {0};
        json_str(json_in, "sub", sub, sizeof(sub));

        if (strcmp(sub, "send") == 0) {
            json_str(json_in, "from", from, sizeof(from));
            json_str(json_in, "to", to, sizeof(to));
            json_str(json_in, "body", body, sizeof(body));
            json_str(json_in, "session", sess, sizeof(sess));
            /* Store message */
            csos_message_t *m = &org->messages[org->msg_head % CSOS_MAX_MESSAGES];
            m->timestamp = (int64_t)time(NULL);
            strncpy(m->from, from[0] ? from : "unknown", sizeof(m->from)-1);
            strncpy(m->to, to[0] ? to : "all", sizeof(m->to)-1);
            /* Sanitize body for JSON safety */
            {
                int bi = 0;
                for (int i = 0; body[i] && bi < (int)sizeof(m->body)-2; i++) {
                    unsigned char c = (unsigned char)body[i];
                    if (c < 32) { if (c == '\n') { m->body[bi++] = ' '; } continue; }
                    if (c == '"' || c == '\\') { m->body[bi++] = ' '; continue; }
                    m->body[bi++] = (char)c;
                }
                m->body[bi] = 0;
            }
            strncpy(m->session, sess, sizeof(m->session)-1);
            m->read = 0;
            org->msg_head++;
            if (org->msg_count < CSOS_MAX_MESSAGES) org->msg_count++;
            /* Log as event too so SSE broadcasts it */
            char emsg[256];
            snprintf(emsg, sizeof(emsg), "[%s→%s] %.*s", m->from, m->to, 180, body);
            csos_event_log(org, strcmp(from,"agent")==0 ? EVT_AGENT_ACTION : EVT_CANVAS_ACTION,
                           m->from, sess, emsg, 0);

            /* ═══ REFLEXIVE PROPAGATION ═══
             * When an agent sends a message, the gradient has changed.
             * Absorb agent activity as a signal into eco_cockpit.
             * This means: agent work = energy flowing through the cockpit ring.
             * Canvas sees it immediately via SSE state broadcast. */
            if (_k && strcmp(from, "agent") != 0) {
                /* Canvas→agent messages get absorbed too */
                csos_membrane_absorb(_k, 8.0, 0, PROTO_STDIO);
            } else if (_k) {
                /* Agent responses are stronger signals (productive work) */
                csos_membrane_absorb(_k, 15.0, 0, PROTO_INTERNAL);
            }

            /* Include message content in response so SSE broadcast carries it.
             * Canvas SSE handler detects "msg_broadcast" and renders in chat. */
            snprintf(json_out, out_sz,
                "{\"sent\":true,\"id\":%d,\"msg_broadcast\":true,"
                "\"from\":\"%s\",\"to\":\"%s\",\"body\":\"%.*s\","
                "\"session\":\"%s\"}",
                org->msg_head-1, m->from, m->to,
                (int)(out_sz - 200), m->body, m->session);
            return 0;
        }

        /* Read messages for a recipient */
        if (strcmp(sub, "read") == 0) {
            json_str(json_in, "for", to, sizeof(to));
            if (!to[0]) strncpy(to, "agent", sizeof(to));
            int pos = 0;
            pos += snprintf(json_out + pos, out_sz - pos, "{\"messages\":[");
            int count = org->msg_count < CSOS_MAX_MESSAGES ? org->msg_count : CSOS_MAX_MESSAGES;
            int first = 1;
            for (int i = 0; i < count && pos < (int)out_sz - 300; i++) {
                int idx = (org->msg_head - count + i);
                if (idx < 0) idx += CSOS_MAX_MESSAGES;
                idx = idx % CSOS_MAX_MESSAGES;
                csos_message_t *m = &org->messages[idx];
                if (strcmp(m->to, to) != 0 && strcmp(m->to, "all") != 0) continue;
                if (!first) pos += snprintf(json_out + pos, out_sz - pos, ",");
                pos += snprintf(json_out + pos, out_sz - pos,
                    "{\"ts\":%lld,\"from\":\"%s\",\"body\":\"%.*s\","
                    "\"session\":\"%s\",\"read\":%d}",
                    (long long)m->timestamp, m->from, 200, m->body,
                    m->session, m->read);
                m->read = 1;
                first = 0;
            }
            pos += snprintf(json_out + pos, out_sz - pos, "]}");
            return 0;
        }

        snprintf(json_out, out_sz, "{\"error\":\"msg sub: send or read\"}");
        return -1;
    }

    /* ═══ EQUATE: The Living Equation — unified vitality view ═══ */
    if (strcmp(action, "equate") == 0) {
        json_str(json_in, "ring", ring_name, sizeof(ring_name));
        if (ring_name[0]) {
            /* Single membrane equation */
            csos_membrane_t *m = csos_organism_find(org, ring_name);
            if (!m) { snprintf(json_out, out_sz, "{\"error\":\"ring not found\"}"); return -1; }
            csos_membrane_equate(m, json_out, out_sz);
        } else {
            /* Full organism living equation */
            csos_organism_equate(org, json_out, out_sz);
        }
        return 0;
    }

    /* ═══ SESSION: Living Equation operations ═══ */
    if (strcmp(action, "session") == 0) {
        char sub[64] = {0}, sid[CSOS_NAME_LEN] = {0};
        json_str(json_in, "sub", sub, sizeof(sub));
        json_str(json_in, "id", sid, sizeof(sid));

        /* session list — all living equations */
        if (strcmp(sub, "list") == 0 || sub[0] == 0) {
            csos_session_see_all(org, json_out, out_sz);
            return 0;
        }

        /* session observe — detailed view of one session */
        if (strcmp(sub, "observe") == 0 && sid[0]) {
            csos_session_t *s = csos_session_find(org, sid);
            if (!s) { snprintf(json_out, out_sz, "{\"error\":\"session not found\"}"); return -1; }
            csos_session_observe(s, json_out, out_sz);
            return 0;
        }

        /* session spawn — create new living equation */
        if (strcmp(sub, "spawn") == 0 && sid[0]) {
            char sub_name[CSOS_NAME_LEN] = {0};
            json_str(json_in, "substrate", sub_name, sizeof(sub_name));
            csos_session_t *s = csos_session_spawn(org, sid, sub_name[0] ? sub_name : sid);
            if (!s) { snprintf(json_out, out_sz, "{\"error\":\"max sessions\"}"); return -1; }
            snprintf(json_out, out_sz,
                "{\"session\":\"%s\",\"substrate\":\"%s\",\"stage\":\"seed\"}",
                s->id, s->substrate);
            return 0;
        }

        /* session bind — connect to external world (stomata + phloem) */
        if (strcmp(sub, "bind") == 0 && sid[0]) {
            csos_session_t *s = csos_session_find(org, sid);
            if (!s) { snprintf(json_out, out_sz, "{\"error\":\"session not found\"}"); return -1; }
            char binding[128]={0}, it[16]={0}, is[512]={0}, et[16]={0}, eg[512]={0};
            json_str(json_in, "binding", binding, sizeof(binding));
            json_str(json_in, "ingress_type", it, sizeof(it));
            json_str(json_in, "ingress_source", is, sizeof(is));
            json_str(json_in, "egress_type", et, sizeof(et));
            json_str(json_in, "egress_target", eg, sizeof(eg));
            /* Also support auth and format */
            char auth[128]={0}, fmt[32]={0};
            json_str(json_in, "auth", auth, sizeof(auth));
            json_str(json_in, "format", fmt, sizeof(fmt));
            csos_session_bind(s, binding, it[0] ? it : NULL, is[0] ? is : NULL,
                              et[0] ? et : NULL, eg[0] ? eg : NULL);
            if (auth[0]) strncpy(s->ingress.auth, auth, sizeof(s->ingress.auth)-1);
            if (fmt[0]) strncpy(s->egress.format, fmt, sizeof(s->egress.format)-1);

            /* ── SYNTHESIS: Auto-infer connection from bind ── */
            if (binding[0]) {
                csos_session_track_connection(s, binding, it[0] ? it : "pipe");
                csos_session_record_flow(s, FLOW_CONNECTION, 0, 1.0, 1,
                                         s->vitality, s->substrate_hash,
                                         "bind");
                csos_session_synthesize(s);
            }

            snprintf(json_out, out_sz,
                "{\"session\":\"%s\",\"binding\":\"%s\","
                "\"ingress\":\"%s:%s\",\"egress\":\"%s:%s\","
                "\"session_vitality\":%.6f,\"connections\":%d}",
                s->id, s->binding,
                s->ingress.type, s->ingress.source,
                s->egress.type, s->egress.target,
                s->synthesis.session_vitality, s->connection_count);
            return 0;
        }

        /* session schedule — set circadian rhythm */
        if (strcmp(sub, "schedule") == 0 && sid[0]) {
            csos_session_t *s = csos_session_find(org, sid);
            if (!s) { snprintf(json_out, out_sz, "{\"error\":\"session not found\"}"); return -1; }
            char interval_s[32]={0}, auto_s[16]={0};
            json_str(json_in, "interval", interval_s, sizeof(interval_s));
            json_str(json_in, "autonomous", auto_s, sizeof(auto_s));
            int interval = interval_s[0] ? atoi(interval_s) : 60;
            int autonomous = auto_s[0] ? (strcmp(auto_s,"true")==0 || atoi(auto_s)) : 1;
            csos_session_schedule(s, interval, autonomous);
            snprintf(json_out, out_sz,
                "{\"session\":\"%s\",\"interval\":%d,\"autonomous\":%s,"
                "\"next_tick\":%lld}",
                s->id, s->schedule.interval_secs,
                s->schedule.autonomous ? "true" : "false",
                (long long)s->schedule.next_tick);
            return 0;
        }

        /* session tick — manually trigger one cycle */
        if (strcmp(sub, "tick") == 0 && sid[0]) {
            csos_session_t *s = csos_session_find(org, sid);
            if (!s) { snprintf(json_out, out_sz, "{\"error\":\"session not found\"}"); return -1; }
            csos_session_tick(org, s, json_out, out_sz);
            return 0;
        }

        /* session tick_all — tick all due sessions now */
        if (strcmp(sub, "tick_all") == 0) {
            int ticked = csos_session_tick_all(org);
            snprintf(json_out, out_sz, "{\"ticked\":%d}", ticked);
            return 0;
        }

        /* session synthesize — full synthesis state of one session */
        if (strcmp(sub, "synthesize") == 0 && sid[0]) {
            csos_session_t *s = csos_session_find(org, sid);
            if (!s) { snprintf(json_out, out_sz, "{\"error\":\"session not found\"}"); return -1; }
            csos_session_synthesize(s);
            csos_session_synthesize_json(s, json_out, out_sz);
            return 0;
        }

        snprintf(json_out, out_sz, "{\"error\":\"unknown session sub: %s\"}", sub);
        return -1;
    }

    /* ═══ GREENHOUSE: session lifecycle + seed bank + convergence ═══ */
    if (strcmp(action, "greenhouse") == 0) {
        char sub[64] = {0}, sid[CSOS_NAME_LEN] = {0}, sub_name[CSOS_NAME_LEN] = {0};
        json_str(json_in, "sub", sub, sizeof(sub));
        json_str(json_in, "session", sid, sizeof(sid));
        json_str(json_in, "substrate", sub_name, sizeof(sub_name));

        if (strcmp(sub, "spawn") == 0 && sid[0]) {
            csos_session_t *s = csos_session_spawn(org, sid, sub_name[0] ? sub_name : NULL);
            if (s) {
                snprintf(json_out, out_sz,
                    "{\"session\":\"%s\",\"substrate\":\"%s\",\"stage\":\"seed\","
                    "\"seeds_planted\":%d}",
                    s->id, s->substrate, s->seeds_planted);
            } else {
                snprintf(json_out, out_sz, "{\"error\":\"max sessions reached\"}");
            }
        } else if (strcmp(sub, "harvest") == 0 && sid[0]) {
            csos_session_t *s = csos_session_find(org, sid);
            if (s) {
                int harvested = csos_seed_harvest(org, s);
                s->seeds_harvested += harvested;
                s->stage = SESSION_HARVEST;
                snprintf(json_out, out_sz,
                    "{\"session\":\"%s\",\"harvested\":%d,\"seed_bank\":%d}",
                    s->id, harvested, org->seed_count);
            } else {
                snprintf(json_out, out_sz, "{\"error\":\"session not found\"}");
            }
        } else if (strcmp(sub, "merge") == 0) {
            char dst_id[CSOS_NAME_LEN] = {0};
            json_str(json_in, "dst", dst_id, sizeof(dst_id));
            csos_session_t *src = csos_session_find(org, sid);
            csos_session_t *dst = csos_session_find(org, dst_id);
            if (src && dst) {
                double conv = csos_session_convergence(org, src, dst);
                int merged = csos_session_merge(org, src, dst);
                snprintf(json_out, out_sz,
                    "{\"convergence\":%.3f,\"merged\":%d,\"threshold\":%.3f}",
                    conv, merged, CSOS_BOYER_THRESHOLD);
            } else {
                snprintf(json_out, out_sz, "{\"error\":\"session not found\"}");
            }
        } else {
            /* Default: show full greenhouse state */
            csos_greenhouse_see(org, json_out, out_sz);
        }
        return 0;
    }

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

    if (strcmp(action, "hash") == 0) {
        json_str(json_in, "substrate", substrate, sizeof(substrate));
        uint32_t h = 0;
        for (const char *p = substrate; *p; p++) h = h * 31 + (uint8_t)*p;
        snprintf(json_out, out_sz, "{\"substrate\":\"%s\",\"hash\":%u}",
                 substrate, 1000 + (h % 9000));
        return 0;
    }

    /* ═══ EXEC: Run CLI command + auto-absorb (replaces daemon exec) ═══ */
    if (strcmp(action, "exec") == 0) {
        char command[4096] = {0};
        json_str(json_in, "command", command, sizeof(command));
        json_str(json_in, "substrate", substrate, sizeof(substrate));
        if (!substrate[0]) strncpy(substrate, "bash", sizeof(substrate));

        /* Law enforcement: reject build commands */
        if (is_build_cmd(command)) {
            snprintf(json_out, out_sz,
                "{\"error\":true,\"law_violation\":\"I\","
                "\"message\":\"BLOCKED: command creates code file\","
                "\"guidance\":\"Use exec for READ/INTERACT commands, not for writing .py/.js/.ts\"}");
            return -1;
        }

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
        /* Detect truncation: try to read one more byte */
        int truncated = 0;
        if (total >= sizeof(data) - 1) {
            char overflow;
            if (fread(&overflow, 1, 1, fp) == 1) truncated = 1;
            /* Drain remaining output to avoid SIGPIPE on pclose */
            char drain[4096];
            while (fread(drain, 1, sizeof(drain), fp) > 0) {}
        }
        int exit_code = pclose(fp);
        data[total] = 0;

        /* Auto-absorb through membrane */
        csos_photon_t ph = csos_organism_absorb(org, substrate, data, PROTO_STDIO);
        csos_membrane_t *d = _d;
        csos_membrane_t *k = _k;
        csos_membrane_t *o = _o;

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
        pos += photon_to_json(&ph, d, k, o, json_out + pos, out_sz - pos);
        snprintf(json_out + pos, out_sz - pos, "}");
        return 0;
    }

    /* ═══ WEB: Fetch URL + auto-absorb ═══ */
    if (strcmp(action, "web") == 0) {
        /* Delegate to system curl for portability */
        char url[2048] = {0};
        json_str(json_in, "url", url, sizeof(url));
        json_str(json_in, "substrate", substrate, sizeof(substrate));
        if (!substrate[0]) {
            /* Extract domain as substrate */
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
        csos_membrane_t *d = _d;
        csos_membrane_t *k = _k;
        csos_membrane_t *o = _o;

        int pos = snprintf(json_out, out_sz,
            "{\"status\":200,\"url\":\"%s\",\"text_length\":%zu,\"physics\":", url, total);
        pos += photon_to_json(&ph, d, k, o, json_out + pos, out_sz - pos);
        snprintf(json_out + pos, out_sz - pos, "}");
        return 0;
    }

    /* ═══ REMEMBER: Store human data ═══ */
    if (strcmp(action, "remember") == 0) {
        char key[256] = {0}, value[4096] = {0};
        json_str(json_in, "key", key, sizeof(key));
        json_str(json_in, "value", value, sizeof(value));

        /* Store in .csos/sessions/human.json (backward compatible) */
        char hpath[512];
        snprintf(hpath, sizeof(hpath), "%s/.csos/sessions", org->root);
        mkdir(hpath, 0755);
        snprintf(hpath, sizeof(hpath), "%s/.csos/sessions/human.json", org->root);

        /* Read existing */
        char existing[32768] = "{}";
        FILE *f = fopen(hpath, "r");
        if (f) { fread(existing, 1, sizeof(existing)-1, f); fclose(f); }

        /* Append/update key (simple: write full file) */
        /* Find closing brace, insert before it */
        char *end = strrchr(existing, '}');
        if (end) {
            char newfile[32768];
            *end = 0;
            size_t elen = strlen(existing);
            if (elen > 2) /* has existing keys */
                snprintf(newfile, sizeof(newfile), "%s,\n  \"%s\": \"%s\"\n}", existing, key, value);
            else
                snprintf(newfile, sizeof(newfile), "{\n  \"%s\": \"%s\"\n}", key, value);
            f = fopen(hpath, "w");
            if (f) { fputs(newfile, f); fclose(f); }
        }

        /* Absorb the remember event into physics */
        char raw[512];
        snprintf(raw, sizeof(raw), "%s %s", key, value);
        csos_organism_absorb(org, "human_profile", raw, PROTO_LLM);

        snprintf(json_out, out_sz, "{\"remembered\":\"%s\"}", key);
        return 0;
    }

    /* ═══ RECALL: Retrieve human data ═══ */
    if (strcmp(action, "recall") == 0) {
        char hpath[512];
        snprintf(hpath, sizeof(hpath), "%s/.csos/sessions/human.json", org->root);
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

        /* Count Calvin atoms per membrane */
        int calvin_d = 0, calvin_k = 0;
        if (d) for (int i = 0; i < d->atom_count; i++)
            if (strncmp(d->atoms[i].name, "calvin_", 7) == 0) calvin_d++;
        if (k) for (int i = 0; i < k->atom_count; i++)
            if (strncmp(k->atoms[i].name, "calvin_", 7) == 0) calvin_k++;

        /* Read known human fields */
        char hpath[512];
        snprintf(hpath, sizeof(hpath), "%s/.csos/sessions/human.json", org->root);
        char human[4096] = "{}";
        FILE *f = fopen(hpath, "r");
        if (f) { fread(human, 1, sizeof(human)-1, f); fclose(f); }

        /* Motor memory top 5 */
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
            /* Ensure directory exists */
            char dir[512];
            strncpy(dir, path, sizeof(dir));
            char *slash = strrchr(dir, '/');
            if (slash) { *slash = 0; mkdir(dir, 0755); }
            FILE *f = fopen(path, "w");
            if (f) { fputs(payload, f); fclose(f); }
            /* Auto-absorb the egress event */
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
    /*
     * Deliveries are WISDOM, not documents. The gate:
     *   1. Absorb the delivery content through the 3-ring cascade
     *   2. If organism says EXECUTE → store as compact photon JSON (IR format)
     *   3. If organism says EXPLORE → store as latest.md only (ephemeral, overwritten)
     *   4. Never store prose reports — the photon IS the delivery
     *
     * This prevents delivery bloat: only Boyer-confirmed insights persist.
     * Everything else is ephemeral (latest.md gets overwritten each time).
     */
    if (strcmp(action, "deliver") == 0) {
        char content[8192] = {0}, dtype[32] = "execute";
        char substrate_name[128] = "deliverable";
        json_str(json_in, "content", content, sizeof(content));
        json_str(json_in, "type", dtype, sizeof(dtype));
        json_str(json_in, "substrate", substrate_name, sizeof(substrate_name));

        /* Absorb through the 3-ring cascade FIRST — physics decides */
        csos_photon_t ph = csos_organism_absorb(org, substrate_name, content, PROTO_INTERNAL);
        csos_membrane_t *d = _d;
        csos_membrane_t *k = _k;
        csos_membrane_t *o = _o;

        int routed = 0;
        char fpath[512];
        snprintf(fpath, sizeof(fpath), "%s/.csos/deliveries", org->root);
        mkdir(fpath, 0755);

        /* Always write ephemeral latest.md (overwritten each delivery) */
        snprintf(fpath, sizeof(fpath), "%s/.csos/deliveries/latest.md", org->root);
        FILE *ff = fopen(fpath, "w");
        if (ff) { fputs(content, ff); fclose(ff); routed++; }

        /* Boyer gate: only EXECUTE decisions get persisted as wisdom */
        if (ph.decision == DECISION_EXECUTE) {
            /* Store as compact photon JSON — the Universal IR format */
            char wisdom_path[512];
            uint32_t sh = 1000 + (ph.substrate_hash % 9000);
            snprintf(wisdom_path, sizeof(wisdom_path),
                "%s/.csos/deliveries/wisdom_%u_c%u.json", org->root, sh, ph.cycle);
            FILE *wf = fopen(wisdom_path, "w");
            if (wf) {
                /* Truncate content to first 200 chars for insight field */
                char insight[256] = {0};
                size_t clen = strlen(content);
                if (clen > 200) clen = 200;
                /* Escape for JSON */
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

        /* Count atoms by type */
        int eq_atoms = 0, calvin_atoms = 0;
        for (int i = 0; i < m->atom_count; i++) {
            if (strncmp(m->atoms[i].name, "calvin_", 7) == 0) calvin_atoms++;
            else eq_atoms++;
        }

        /* Count resonated vs total photons */
        int total_ph = 0, res_ph = 0;
        for (int i = 0; i < m->atom_count; i++) {
            total_ph += m->atoms[i].photon_count;
            res_ph += (int)atom_gradient(&m->atoms[i]);
        }

        /* Top motor substrates */
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
        snprintf(json_out, out_sz, "{\"saved\":%d,\"rings\":%d}", saved, org->count);
        return 0;
    }

    /* ═══ TOOL: Write to sanctioned directories ONLY ═══ */
    /*
     * This is how agents evolve the system's own tooling.
     * Writes are allowed ONLY to:
     *   .opencode/tools/*.ts      — Tool definitions
     *   .opencode/skills/**       — Skill docs
     *   .opencode/agents/*.md     — Agent definitions
     *   specs/*.csos              — Substrate specs
     *   .csos/deliveries/*        — Deliverables
     *
     * Every write is auto-absorbed into the membrane.
     * Motor memory tracks which tools get created/updated.
     * The system learns from its own evolution.
     */
    if (strcmp(action, "tool") == 0) {
        char filepath[512] = {0}, filecontent[65536] = {0};
        json_str(json_in, "path", filepath, sizeof(filepath));
        json_str(json_in, "body", filecontent, sizeof(filecontent));

        if (!filepath[0]) {
            snprintf(json_out, out_sz, "{\"error\":\"path required\"}");
            return -1;
        }

        /* Law I: only sanctioned paths */
        if (!is_sanctioned_path(filepath)) {
            snprintf(json_out, out_sz,
                "{\"error\":true,\"law_violation\":\"I\","
                "\"message\":\"BLOCKED: path '%s' is outside sanctioned directories\","
                "\"sanctioned\":[\".opencode/tools/\",\".opencode/skills/\","
                "\".opencode/agents/\",\"specs/\",\".csos/deliveries/\"]}",
                filepath);
            return -1;
        }

        /* Ensure parent directory exists */
        char dir[512];
        strncpy(dir, filepath, sizeof(dir));
        char *slash = strrchr(dir, '/');
        if (slash) {
            *slash = 0;
            /* Recursive mkdir for nested paths */
            char tmp[512] = {0};
            for (char *p = dir; *p; p++) {
                tmp[p - dir] = *p;
                if (*p == '/') mkdir(tmp, 0755);
            }
            mkdir(dir, 0755);
        }

        /* Write the file */
        FILE *f = fopen(filepath, "w");
        if (!f) {
            snprintf(json_out, out_sz, "{\"error\":\"cannot write: %s\"}", filepath);
            return -1;
        }
        fputs(filecontent, f);
        fclose(f);

        /* IR VALIDATION: If writing a .csos spec, validate against foundation.
         * Invalid specs are deleted immediately — the system protects itself. */
        if (strstr(filepath, "specs/") && strstr(filepath, ".csos")) {
            csos_spec_t test_spec = {0};
            if (csos_spec_parse(filepath, &test_spec) == 0) {
                /* spec_parse already strips non-foundation atoms.
                 * If nothing survived, the spec is entirely non-physics. Delete it. */
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

        /* Auto-absorb the tool creation event */
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
        /* Escape for JSON */
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
        /* Use ls to list */
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

    /* ═══ WORKFLOW: Draft, validate, and execute substrate pipelines ═══ */
    /*
     * A workflow is a DAG of substrates. Each node is a substrate name.
     * Each edge is a Forster coupling (data flows between stages).
     * The workflow itself is absorbed into the membrane — Boyer decides
     * when the pipeline has enough evidence to EXECUTE.
     *
     * No new formulas. Workflow nodes are substrates. The 5 equations
     * process them. Calvin discovers patterns across workflow runs.
     *
     * Format: Mermaid-style text → parsed into substrate chain.
     *   "A[ingest] --> B[transform] --> C[load]"
     *   becomes: absorb(A), absorb(B), absorb(C) with Forster coupling
     *
     * Auth ladder gates source registration:
     *   Level 0: none (local substrates only)
     *   Level 1: api_key (registered external sources)
     *   Level 2: token (JWT-authenticated sources)
     *   Level 3: oauth (full OAuth2 flow sources)
     */

    if (strcmp(action, "workflow") == 0) {
        char subaction[64] = {0}, wf_spec[8192] = {0}, wf_name[128] = {0};
        json_str(json_in, "sub", subaction, sizeof(subaction));
        json_str(json_in, "spec", wf_spec, sizeof(wf_spec));
        json_str(json_in, "name", wf_name, sizeof(wf_name));
        if (!wf_name[0]) strncpy(wf_name, "unnamed", sizeof(wf_name));

        /* ── DRAFT: Parse Mermaid-like spec into substrate nodes ── */
        if (strcmp(subaction, "draft") == 0 || !subaction[0]) {
            if (!wf_spec[0]) {
                snprintf(json_out, out_sz, "{\"error\":\"spec required (Mermaid-like workflow)\"}");
                return -1;
            }

            /* Parse nodes from "A[label] --> B[label] --> C[label]" */
            /* Also supports: "A[label]\\nB[label]\\nC[label]" (newline-separated) */
            char nodes[32][128];
            char labels[32][128];
            int node_count = 0;
            const char *p = wf_spec;

            while (*p && node_count < 32) {
                /* Skip whitespace and arrows */
                while (*p && (*p == ' ' || *p == '-' || *p == '>' || *p == '\n' || *p == '\\')) {
                    if (*p == '\\' && *(p+1) == 'n') p++; /* skip \n literal */
                    p++;
                }
                if (!*p) break;

                /* Read node ID */
                int ni = 0;
                while (*p && *p != '[' && *p != ' ' && *p != '-' && *p != '\n'
                       && *p != '\\' && ni < 127) {
                    nodes[node_count][ni++] = *p++;
                }
                nodes[node_count][ni] = 0;
                if (ni == 0) continue;

                /* Read label if present */
                labels[node_count][0] = 0;
                if (*p == '[') {
                    p++;
                    int li = 0;
                    while (*p && *p != ']' && li < 127) labels[node_count][li++] = *p++;
                    labels[node_count][li] = 0;
                    if (*p == ']') p++;
                } else {
                    strncpy(labels[node_count], nodes[node_count], 127);
                }
                node_count++;
            }

            if (node_count == 0) {
                snprintf(json_out, out_sz, "{\"error\":\"no nodes parsed from spec\"}");
                return -1;
            }

            /* Absorb the workflow draft through the 3-ring cascade */
            char raw[512];
            snprintf(raw, sizeof(raw), "workflow_draft %s nodes=%d", wf_name, node_count);
            csos_photon_t ph = csos_organism_absorb(org, "workflow", raw, PROTO_LLM);

            /* ── SYNTHESIS: Auto-infer workflow into active sessions ── */
            /* Every workflow drafted in this agentic environment becomes an
             * element of ALL active sessions' living equations.
             * Biology: a new metabolic pathway activating in the cell. */
            for (int si = 0; si < org->session_count; si++) {
                csos_session_t *sess = &org->sessions[si];
                if (sess->stage == SESSION_DORMANT) continue;
                csos_session_track_workflow(sess, wf_name, node_count);
                csos_session_record_flow(sess, FLOW_WORKFLOW, ph.cycle,
                                         (double)node_count, ph.delta,
                                         ph.vitality, sess->substrate_hash,
                                         wf_name);
                csos_session_synthesize(sess);
            }

            /* Auto-save spec version */
            {
                char vpath[512];
                snprintf(vpath, sizeof(vpath), "%s/.csos/sessions", org->root);
                mkdir(vpath, 0755);
                snprintf(vpath, sizeof(vpath), "%s/.csos/sessions/spec_versions.json", org->root);
                char *vexist = (char *)calloc(1, 32768);
                if (vexist) {
                    strcpy(vexist, "{\"versions\":[]}");
                    FILE *vf = fopen(vpath, "r");
                    if (vf) { fread(vexist, 1, 32767, vf); fclose(vf); }
                    char *arr_end = strstr(vexist, "]}");
                    if (arr_end) {
                        char *vnew = (char *)calloc(1, 32768);
                        if (vnew) {
                            int has_entries = (arr_end - vexist) > 15;
                            *arr_end = 0;
                            char esc_spec[2048] = {0};
                            size_t esi = 0;
                            for (size_t si = 0; wf_spec[si] && esi < sizeof(esc_spec) - 4; si++) {
                                if (wf_spec[si] == '"') { esc_spec[esi++] = '\\'; esc_spec[esi++] = '"'; }
                                else if (wf_spec[si] == '\\') { esc_spec[esi++] = '\\'; esc_spec[esi++] = '\\'; }
                                else if (wf_spec[si] == '\n') { esc_spec[esi++] = '\\'; esc_spec[esi++] = 'n'; }
                                else if ((unsigned char)wf_spec[si] >= 32) { esc_spec[esi++] = wf_spec[si]; }
                            }
                            esc_spec[esi] = 0;
                            snprintf(vnew, 32768,
                                "%s%s{\"name\":\"%s\",\"spec\":\"%s\",\"mermaid\":\"\","
                                "\"timestamp\":%ld,\"type\":\"draft\"}]}",
                                vexist, has_entries ? "," : "",
                                wf_name, esc_spec, (long)time(NULL));
                            vf = fopen(vpath, "w");
                            if (vf) { fputs(vnew, vf); fclose(vf); }
                            free(vnew);
                        }
                    }
                    free(vexist);
                }
            }

            /* Build response with node list + completions */
            int pos = snprintf(json_out, out_sz,
                "{\"workflow\":\"%s\",\"nodes\":%d,\"stages\":[", wf_name, node_count);
            for (int i = 0; i < node_count && pos < (int)out_sz - 256; i++) {
                if (i > 0) pos += snprintf(json_out + pos, out_sz - pos, ",");
                /* Compute hash for motor memory lookup */
                uint32_t h = 0;
                for (const char *c = nodes[i]; *c; c++) h = h * 31 + (uint8_t)*c;
                h = 1000 + (h % 9000);
                double strength = csos_motor_strength(
                    _d, h);

                pos += snprintf(json_out + pos, out_sz - pos,
                    "{\"id\":\"%s\",\"label\":\"%s\",\"hash\":%u,"
                    "\"motor_strength\":%.3f,\"status\":\"pending\"}",
                    nodes[i], labels[i], h, strength);
            }
            pos += snprintf(json_out + pos, out_sz - pos,
                "],\"edges\":[");
            for (int i = 0; i < node_count - 1 && pos < (int)out_sz - 128; i++) {
                if (i > 0) pos += snprintf(json_out + pos, out_sz - pos, ",");
                pos += snprintf(json_out + pos, out_sz - pos,
                    "{\"from\":\"%s\",\"to\":\"%s\"}", nodes[i], nodes[i+1]);
            }
            pos += snprintf(json_out + pos, out_sz - pos,
                "],\"delta\":%d,\"decision\":\"%s\"}",
                ph.delta, (const char*[]){"EXPLORE","EXECUTE","ASK","STORE"}[ph.decision & 3]);
            return 0;
        }

        /* ── RUN: Execute each node as a substrate absorption ── */
        if (strcmp(subaction, "run") == 0) {
            if (!wf_spec[0]) {
                snprintf(json_out, out_sz, "{\"error\":\"spec required\"}");
                return -1;
            }

            /* Re-parse nodes (same parser as draft) */
            char nodes[32][128];
            int node_count = 0;
            const char *rp = wf_spec;
            while (*rp && node_count < 32) {
                while (*rp && (*rp == ' ' || *rp == '-' || *rp == '>' || *rp == '\n' || *rp == '\\')) {
                    if (*rp == '\\' && *(rp+1) == 'n') rp++;
                    rp++;
                }
                if (!*rp) break;
                int ni = 0;
                while (*rp && *rp != '[' && *rp != ' ' && *rp != '-' && *rp != '\n'
                       && *rp != '\\' && ni < 127) {
                    nodes[node_count][ni++] = *rp++;
                }
                nodes[node_count][ni] = 0;
                if (ni == 0) continue;
                if (*rp == '[') { while (*rp && *rp != ']') rp++; if (*rp) rp++; }
                node_count++;
            }

            /* Load node configs for real execution */
            char cfgpath[512];
            snprintf(cfgpath, sizeof(cfgpath), "%s/.csos/sessions/node_configs.json", org->root);
            char cfgdata[32768] = "{}";
            FILE *cfgf = fopen(cfgpath, "r");
            if (cfgf) { fread(cfgdata, 1, sizeof(cfgdata)-1, cfgf); fclose(cfgf); }

            /* Execute each node as a substrate absorption in sequence.
             * Each node's output feeds the next via Forster coupling.
             * If node has configured command, execute it via popen(). */
            int pos = snprintf(json_out, out_sz,
                "{\"workflow\":\"%s\",\"executed\":%d,\"results\":[", wf_name, node_count);
            csos_photon_t last = {0};
            for (int i = 0; i < node_count && pos < (int)out_sz - 512; i++) {
                /* Check if this node has a configured command */
                char node_cmd[4096] = {0};
                int executed = 0, exit_code = 0;
                char cmd_output[4096] = {0};

                /* Look for configured command: find wf_name section, then node */
                char *wf_sec = strstr(cfgdata, wf_name);
                if (wf_sec) {
                    char *nd_sec = strstr(wf_sec, nodes[i]);
                    if (nd_sec) {
                        char *cmd_key = strstr(nd_sec, "\"command\":\"");
                        if (cmd_key) {
                            cmd_key += 11; /* skip "command":" */
                            size_t ci = 0;
                            while (*cmd_key && *cmd_key != '"' && ci < sizeof(node_cmd) - 1)
                                node_cmd[ci++] = *cmd_key++;
                            node_cmd[ci] = 0;
                        }
                    }
                }

                /* Fall back to default command template if no config */
                if (!node_cmd[0]) {
                    const char *def = node_type_command(nodes[i]);
                    if (def) strncpy(node_cmd, def, sizeof(node_cmd) - 1);
                }

                /* Execute command if available */
                if (node_cmd[0]) {
                    /* Law enforcement: reject build commands */
                    if (is_build_cmd(node_cmd)) {
                        snprintf(cmd_output, sizeof(cmd_output),
                            "BLOCKED: Law I violation in node %s", nodes[i]);
                        exit_code = -1;
                    } else {
                        FILE *fp = popen(node_cmd, "r");
                        if (fp) {
                            size_t total = 0;
                            while (total < sizeof(cmd_output) - 1) {
                                size_t n = fread(cmd_output + total, 1,
                                    sizeof(cmd_output) - 1 - total, fp);
                                if (n == 0) break;
                                total += n;
                            }
                            /* Drain remaining output to avoid SIGPIPE */
                            if (total >= sizeof(cmd_output) - 1) {
                                char drain[4096];
                                while (fread(drain, 1, sizeof(drain), fp) > 0) {}
                            }
                            exit_code = pclose(fp);
                            cmd_output[total] = 0;
                            executed = 1;
                        } else {
                            snprintf(cmd_output, sizeof(cmd_output), "popen failed");
                            exit_code = -1;
                        }
                    }

                    /* Absorb REAL output through membrane */
                    char raw[512];
                    snprintf(raw, sizeof(raw), "stage_%d %s exit=%d %.200s",
                             i, nodes[i], exit_code, cmd_output);
                    last = csos_organism_absorb(org, nodes[i], raw, PROTO_STDIO);
                } else {
                    /* No command — existing behavior: absorb stage name */
                    char raw[256];
                    snprintf(raw, sizeof(raw), "stage_%d %s delta=%d",
                             i, nodes[i], last.delta);
                    last = csos_organism_absorb(org, nodes[i], raw, PROTO_LLM);
                }

                /* ── SYNTHESIS: Track workflow step across all active sessions ── */
                {
                    int step_ok = (exit_code == 0 && last.delta >= 0);
                    for (int si = 0; si < org->session_count; si++) {
                        csos_session_t *sess = &org->sessions[si];
                        if (sess->stage == SESSION_DORMANT) continue;
                        csos_session_workflow_step(sess, wf_name, step_ok);
                        char step_label[64];
                        snprintf(step_label, sizeof(step_label), "%s:%s", wf_name, nodes[i]);
                        csos_session_record_flow(sess, FLOW_WORKFLOW, last.cycle,
                                                 last.actual, last.delta, last.vitality,
                                                 sess->substrate_hash, step_label);
                        csos_session_synthesize(sess);
                    }
                }

                /* Escape command output for JSON */
                char escaped[2048] = {0};
                size_t ei = 0;
                for (size_t j = 0; cmd_output[j] && ei < sizeof(escaped) - 4; j++) {
                    if (cmd_output[j] == '"') { escaped[ei++] = '\\'; escaped[ei++] = '"'; }
                    else if (cmd_output[j] == '\\') { escaped[ei++] = '\\'; escaped[ei++] = '\\'; }
                    else if (cmd_output[j] == '\n') { escaped[ei++] = '\\'; escaped[ei++] = 'n'; }
                    else if (cmd_output[j] == '\r') { /* skip */ }
                    else if ((unsigned char)cmd_output[j] >= 32) { escaped[ei++] = cmd_output[j]; }
                }
                escaped[ei] = 0;

                if (i > 0) pos += snprintf(json_out + pos, out_sz - pos, ",");
                pos += snprintf(json_out + pos, out_sz - pos,
                    "{\"stage\":%d,\"node\":\"%s\",\"decision\":\"%s\","
                    "\"delta\":%d,\"motor\":%.3f,\"resonated\":%s,"
                    "\"executed\":%s,\"exit_code\":%d,\"output\":\"%.1500s\"}",
                    i, nodes[i],
                    (const char*[]){"EXPLORE","EXECUTE","ASK","STORE"}[last.decision & 3],
                    last.delta, last.motor_strength,
                    last.resonated ? "true" : "false",
                    executed ? "true" : "false", exit_code, escaped);
            }
            pos += snprintf(json_out + pos, out_sz - pos,
                "],\"final_decision\":\"%s\",\"total_delta\":%d}",
                (const char*[]){"EXPLORE","EXECUTE","ASK","STORE"}[last.decision & 3],
                last.delta);
            return 0;
        }

        /* ── CONFIGURE: Set execution config for a workflow node ── */
        if (strcmp(subaction, "configure") == 0) {
            char node_id[128] = {0}, cmd_str[4096] = {0};
            char env_str[2048] = {0}, timeout_str[32] = {0};
            json_str(json_in, "node", node_id, sizeof(node_id));
            json_str(json_in, "command", cmd_str, sizeof(cmd_str));
            json_str(json_in, "env", env_str, sizeof(env_str));
            json_str(json_in, "timeout", timeout_str, sizeof(timeout_str));

            if (!wf_name[0] || !node_id[0]) {
                snprintf(json_out, out_sz, "{\"error\":\"name and node required\"}");
                return -1;
            }

            /* Store config in .csos/sessions/node_configs.json */
            char cfgpath[512];
            snprintf(cfgpath, sizeof(cfgpath), "%s/.csos/sessions", org->root);
            mkdir(cfgpath, 0755);
            snprintf(cfgpath, sizeof(cfgpath), "%s/.csos/sessions/node_configs.json", org->root);

            /* Read existing */
            char existing[32768] = "{}";
            FILE *f = fopen(cfgpath, "r");
            if (f) { fread(existing, 1, sizeof(existing)-1, f); fclose(f); }

            /* Write updated (simple: rewrite entire file with new entry) */
            char newfile[32768];
            char *wf_section = strstr(existing, wf_name);
            if (!wf_section) {
                /* Add new workflow section */
                char *end = strrchr(existing, '}');
                if (end) {
                    *end = 0;
                    size_t elen = strlen(existing);
                    snprintf(newfile, sizeof(newfile),
                        "%s%s\"%s\":{\"%s\":{\"command\":\"%s\",\"env\":\"%s\",\"timeout\":%s}}}",
                        existing, elen > 2 ? "," : "",
                        wf_name, node_id, cmd_str, env_str,
                        timeout_str[0] ? timeout_str : "30");
                }
            } else {
                /* Simplified: rewrite with updated entry */
                snprintf(newfile, sizeof(newfile),
                    "{\"%s\":{\"%s\":{\"command\":\"%s\",\"env\":\"%s\",\"timeout\":%s}}}",
                    wf_name, node_id, cmd_str, env_str,
                    timeout_str[0] ? timeout_str : "30");
            }
            f = fopen(cfgpath, "w");
            if (f) { fputs(newfile, f); fclose(f); }

            /* Absorb config event */
            char raw[512];
            snprintf(raw, sizeof(raw), "node_configure %s/%s", wf_name, node_id);
            csos_photon_t ph = csos_organism_absorb(org, node_id, raw, PROTO_LLM);

            snprintf(json_out, out_sz,
                "{\"configured\":\"%s\",\"node\":\"%s\",\"command\":\"%s\","
                "\"delta\":%d,\"decision\":\"%s\"}",
                wf_name, node_id, cmd_str,
                ph.delta, (const char*[]){"EXPLORE","EXECUTE","ASK","STORE"}[ph.decision & 3]);
            return 0;
        }

        /* ── RUN_STEP: Execute a single workflow node with real commands ── */
        if (strcmp(subaction, "run_step") == 0) {
            if (!wf_spec[0]) {
                snprintf(json_out, out_sz, "{\"error\":\"spec required\"}");
                return -1;
            }

            /* Parse step index from JSON */
            char step_str[16] = {0};
            json_str(json_in, "step", step_str, sizeof(step_str));
            int step = step_str[0] ? atoi(step_str) : 0;

            /* Re-parse nodes (same parser as draft/run) */
            char nodes[32][128];
            int node_count = 0;
            const char *rp = wf_spec;
            while (*rp && node_count < 32) {
                while (*rp && (*rp == ' ' || *rp == '-' || *rp == '>' || *rp == '\n' || *rp == '\\')) {
                    if (*rp == '\\' && *(rp+1) == 'n') rp++;
                    rp++;
                }
                if (!*rp) break;
                int ni = 0;
                while (*rp && *rp != '[' && *rp != ' ' && *rp != '-' && *rp != '\n'
                       && *rp != '\\' && ni < 127) {
                    nodes[node_count][ni++] = *rp++;
                }
                nodes[node_count][ni] = 0;
                if (ni == 0) continue;
                if (*rp == '[') { while (*rp && *rp != ']') rp++; if (*rp) rp++; }
                node_count++;
            }

            if (step < 0 || step >= node_count) {
                snprintf(json_out, out_sz,
                    "{\"error\":\"step %d out of range (0-%d)\"}", step, node_count - 1);
                return -1;
            }

            /* Load node config */
            char cfgpath[512];
            snprintf(cfgpath, sizeof(cfgpath), "%s/.csos/sessions/node_configs.json", org->root);
            char cfgdata[32768] = "{}";
            FILE *cfgf = fopen(cfgpath, "r");
            if (cfgf) { fread(cfgdata, 1, sizeof(cfgdata)-1, cfgf); fclose(cfgf); }

            char node_cmd[4096] = {0};
            int executed = 0, exit_code = 0;
            char cmd_output[4096] = {0};

            /* Look for configured command */
            char *wf_sec = strstr(cfgdata, wf_name);
            if (wf_sec) {
                char *nd_sec = strstr(wf_sec, nodes[step]);
                if (nd_sec) {
                    char *cmd_key = strstr(nd_sec, "\"command\":\"");
                    if (cmd_key) {
                        cmd_key += 11;
                        size_t ci = 0;
                        while (*cmd_key && *cmd_key != '"' && ci < sizeof(node_cmd) - 1)
                            node_cmd[ci++] = *cmd_key++;
                        node_cmd[ci] = 0;
                    }
                }
            }

            /* Fall back to default command template */
            if (!node_cmd[0]) {
                const char *def = node_type_command(nodes[step]);
                if (def) strncpy(node_cmd, def, sizeof(node_cmd) - 1);
            }

            csos_photon_t ph = {0};
            if (node_cmd[0]) {
                /* Law enforcement */
                if (is_build_cmd(node_cmd)) {
                    snprintf(cmd_output, sizeof(cmd_output),
                        "BLOCKED: Law I violation in node %s", nodes[step]);
                    exit_code = -1;
                } else {
                    FILE *fp = popen(node_cmd, "r");
                    if (fp) {
                        size_t total = 0;
                        while (total < sizeof(cmd_output) - 1) {
                            size_t n = fread(cmd_output + total, 1,
                                sizeof(cmd_output) - 1 - total, fp);
                            if (n == 0) break;
                            total += n;
                        }
                        if (total >= sizeof(cmd_output) - 1) {
                            char drain[4096];
                            while (fread(drain, 1, sizeof(drain), fp) > 0) {}
                        }
                        exit_code = pclose(fp);
                        cmd_output[total] = 0;
                        executed = 1;
                    } else {
                        snprintf(cmd_output, sizeof(cmd_output), "popen failed");
                        exit_code = -1;
                    }
                }
                char raw[512];
                snprintf(raw, sizeof(raw), "step_%d %s exit=%d %.200s",
                         step, nodes[step], exit_code, cmd_output);
                ph = csos_organism_absorb(org, nodes[step], raw, PROTO_STDIO);
            } else {
                char raw[256];
                snprintf(raw, sizeof(raw), "step_%d %s", step, nodes[step]);
                ph = csos_organism_absorb(org, nodes[step], raw, PROTO_LLM);
            }

            /* Escape output for JSON */
            char escaped[2048] = {0};
            size_t ei = 0;
            for (size_t j = 0; cmd_output[j] && ei < sizeof(escaped) - 4; j++) {
                if (cmd_output[j] == '"') { escaped[ei++] = '\\'; escaped[ei++] = '"'; }
                else if (cmd_output[j] == '\\') { escaped[ei++] = '\\'; escaped[ei++] = '\\'; }
                else if (cmd_output[j] == '\n') { escaped[ei++] = '\\'; escaped[ei++] = 'n'; }
                else if (cmd_output[j] == '\r') { /* skip */ }
                else if ((unsigned char)cmd_output[j] >= 32) { escaped[ei++] = cmd_output[j]; }
            }
            escaped[ei] = 0;

            snprintf(json_out, out_sz,
                "{\"step\":%d,\"node\":\"%s\",\"executed\":%s,"
                "\"output\":\"%.1500s\",\"exit_code\":%d,"
                "\"decision\":\"%s\",\"delta\":%d,\"motor\":%.3f}",
                step, nodes[step], executed ? "true" : "false",
                escaped, exit_code,
                (const char*[]){"EXPLORE","EXECUTE","ASK","STORE"}[ph.decision & 3],
                ph.delta, ph.motor_strength);
            return 0;
        }

        /* ── VERSIONS: List stored spec versions ── */
        if (strcmp(subaction, "versions") == 0) {
            char vpath[512];
            snprintf(vpath, sizeof(vpath), "%s/.csos/sessions/spec_versions.json", org->root);
            FILE *f = fopen(vpath, "r");
            if (!f) { snprintf(json_out, out_sz, "{\"versions\":[]}"); return 0; }
            char data[32768] = {0};
            fread(data, 1, sizeof(data)-1, f); fclose(f);
            snprintf(json_out, out_sz, "%s", data);
            return 0;
        }

        /* ── RESTORE: Fetch a specific spec version by index ── */
        if (strcmp(subaction, "restore") == 0) {
            char idx_str[16] = {0};
            json_str(json_in, "index", idx_str, sizeof(idx_str));
            int idx = idx_str[0] ? atoi(idx_str) : 0;

            char vpath[512];
            snprintf(vpath, sizeof(vpath), "%s/.csos/sessions/spec_versions.json", org->root);
            FILE *f = fopen(vpath, "r");
            if (!f) {
                snprintf(json_out, out_sz, "{\"error\":\"no versions found\"}");
                return -1;
            }
            char data[32768] = {0};
            fread(data, 1, sizeof(data)-1, f); fclose(f);

            /* Walk to the Nth version entry */
            char *p = data;
            int count = 0;
            while (count < idx) {
                p = strstr(p, "\"name\":");
                if (!p) break;
                p++; count++;
            }
            if (count < idx || !strstr(p, "\"name\":")) {
                snprintf(json_out, out_sz, "{\"error\":\"version %d not found\"}", idx);
                return -1;
            }

            /* Extract name, spec, mermaid from this entry */
            char vname[128] = {0}, vspec[4096] = {0}, vmermaid[4096] = {0};
            char *entry = strstr(p, "\"name\":");
            if (entry) {
                /* Simple extraction from the entry's JSON context */
                json_str(entry, "name", vname, sizeof(vname));
                json_str(entry, "spec", vspec, sizeof(vspec));
                json_str(entry, "mermaid", vmermaid, sizeof(vmermaid));
            }

            snprintf(json_out, out_sz,
                "{\"restored\":true,\"index\":%d,\"name\":\"%s\","
                "\"spec\":\"%s\",\"mermaid\":\"%s\"}",
                idx, vname, vspec, vmermaid);
            return 0;
        }

        /* ── COMPLETE: Tab-completion from motor memory ── */
        if (strcmp(subaction, "complete") == 0) {
            char prefix[128] = {0};
            json_str(json_in, "prefix", prefix, sizeof(prefix));

            /* Get top motor entries from eco_domain (known substrates) */
            csos_membrane_t *d = _d;
            uint32_t h[20]; double s[20];
            int n = d ? csos_motor_top(d, h, s, 20) : 0;

            /* Also collect Calvin atom names (discovered patterns) */
            int pos = snprintf(json_out, out_sz,
                "{\"completions\":[");
            int first = 1;

            /* Motor memory substrates (hashes — agent maps to names) */
            for (int i = 0; i < n && pos < (int)out_sz - 128; i++) {
                if (s[i] < 0.01) continue; /* Skip near-zero strength */
                if (!first) pos += snprintf(json_out + pos, out_sz - pos, ",");
                pos += snprintf(json_out + pos, out_sz - pos,
                    "{\"hash\":%u,\"strength\":%.3f,\"type\":\"substrate\"}",
                    h[i], s[i]);
                first = 0;
            }

            /* Calvin atoms (discovered patterns — named) */
            if (d) {
                for (int i = 0; i < d->atom_count && pos < (int)out_sz - 256; i++) {
                    if (strncmp(d->atoms[i].name, "calvin_", 7) != 0) continue;
                    if (prefix[0] && strncmp(d->atoms[i].name, prefix, strlen(prefix)) != 0)
                        continue;
                    if (!first) pos += snprintf(json_out + pos, out_sz - pos, ",");
                    pos += snprintf(json_out + pos, out_sz - pos,
                        "{\"name\":\"%s\",\"formula\":\"%s\",\"type\":\"pattern\"}",
                        d->atoms[i].name, d->atoms[i].formula);
                    first = 0;
                }
            }

            /* Foundation atoms (always available) */
            if (d) {
                for (int i = 0; i < d->atom_count && pos < (int)out_sz - 256; i++) {
                    if (strncmp(d->atoms[i].name, "calvin_", 7) == 0) continue;
                    if (!first) pos += snprintf(json_out + pos, out_sz - pos, ",");
                    pos += snprintf(json_out + pos, out_sz - pos,
                        "{\"name\":\"%s\",\"source\":\"%s\",\"type\":\"foundation\"}",
                        d->atoms[i].name, d->atoms[i].source);
                    first = 0;
                }
            }

            snprintf(json_out + pos, out_sz - pos, "]}");
            return 0;
        }

        /* ── SYNTHESIZE: Describe intent → system produces Mermaid + metadata ── */
        /*
         * Takes a natural-language description. Maps keywords to known substrate
         * patterns via motor memory + template catalog. Produces structured
         * Mermaid with per-node metadata: processing unit, libs, runtime, state.
         *
         * This is NOT an LLM call. It's pattern matching against:
         *   1. Template catalog (6 predefined workflows)
         *   2. Motor memory (known substrates ranked by strength)
         *   3. Registered auth sources (external data endpoints)
         *   4. Calvin patterns (discovered cross-workflow correlations)
         *
         * The agent can call this to bootstrap a workflow from user intent,
         * then the user refines in the Mermaid editor.
         */
        if (strcmp(subaction, "synthesize") == 0) {
            char description[2048] = {0};
            json_str(json_in, "description", description, sizeof(description));
            if (!description[0]) {
                snprintf(json_out, out_sz, "{\"error\":\"description required\"}");
                return -1;
            }

            /* Keyword → processing unit mapping (derived from foundation equations) */
            typedef struct { const char *kw; const char *id; const char *label;
                             const char *proc; const char *libs; const char *runtime; } kw_map_t;
            static const kw_map_t KWMAP[] = {
                {"fetch",     "fetch",     "Fetch Data",     "http_client",   "curl,tls",       "native"},
                {"ingest",    "ingest",    "Ingest",         "stream_reader", "io,buffer",      "native"},
                {"api",       "api_call",  "API Call",       "http_client",   "curl,json",      "native"},
                {"postgres",  "pg_query",  "Postgres Query", "db_driver",     "libpq,sql",      "native"},
                {"mysql",     "mysql_q",   "MySQL Query",    "db_driver",     "mysqlclient",    "native"},
                {"redis",     "redis_op",  "Redis Op",       "cache_driver",  "hiredis",        "native"},
                {"s3",        "s3_io",     "S3 Transfer",    "cloud_storage", "aws_sdk,s3",     "cloud"},
                {"gcs",       "gcs_io",    "GCS Transfer",   "cloud_storage", "gcloud_sdk",     "cloud"},
                {"transform", "transform", "Transform",      "data_proc",     "jq,awk",         "native"},
                {"parse",     "parse",     "Parse",          "parser",        "json,csv,regex",  "native"},
                {"json",      "json_proc", "JSON Process",   "parser",        "json",           "native"},
                {"csv",       "csv_proc",  "CSV Process",    "parser",        "csv",            "native"},
                {"validate",  "validate",  "Validate",       "validator",     "schema,assert",  "native"},
                {"schema",    "schema_ck", "Schema Check",   "validator",     "jsonschema",     "native"},
                {"filter",    "filter",    "Filter",         "data_proc",     "jq,grep",        "native"},
                {"aggregate", "aggregate", "Aggregate",      "data_proc",     "awk,reduce",     "native"},
                {"ml",        "ml_infer",  "ML Inference",   "model_runtime", "onnx,numpy",     "python"},
                {"model",     "model_run", "Model Run",      "model_runtime", "torch,tf",       "python"},
                {"predict",   "predict",   "Predict",        "model_runtime", "sklearn",        "python"},
                {"train",     "train",     "Train Model",    "gpu_compute",   "torch,cuda",     "gpu"},
                {"embed",     "embed",     "Embedding",      "model_runtime", "sentence_tf",    "python"},
                {"load",      "load",      "Load",           "data_writer",   "io,bulk",        "native"},
                {"store",     "store",     "Store",          "data_writer",   "io,db",          "native"},
                {"write",     "write_out", "Write Output",   "data_writer",   "io,fs",          "native"},
                {"deliver",   "deliver",   "Deliver",        "egress",        "http,webhook",   "native"},
                {"ship",      "ship",      "Ship Result",    "egress",        "http,slack",     "native"},
                {"notify",    "notify",    "Notify",         "egress",        "smtp,webhook",   "native"},
                {"deploy",    "deploy",    "Deploy",         "orchestrator",  "docker,k8s",     "cloud"},
                {"test",      "test",      "Test",           "validator",     "assert,diff",    "native"},
                {"auth",      "auth_ck",   "Auth Check",     "auth_gate",     "jwt,oauth",      "native"},
                {"encrypt",   "encrypt",   "Encrypt",        "crypto",        "openssl,aes",    "native"},
                {"compress",  "compress",  "Compress",       "packer",        "gzip,zstd",      "native"},
                {"cache",     "cache",     "Cache",          "cache_driver",  "redis,mmap",     "native"},
                {"queue",     "enqueue",   "Enqueue",        "msg_broker",    "amqp,redis",     "native"},
                {"schedule",  "schedule",  "Schedule",       "orchestrator",  "cron,temporal",  "native"},
                {"retry",     "retry",     "Retry",          "orchestrator",  "backoff,circuit","native"},
                {"log",       "log",       "Log",            "observer",      "syslog,json",    "native"},
                {"monitor",   "monitor",   "Monitor",        "observer",      "metrics,prom",   "native"},
                {NULL, NULL, NULL, NULL, NULL, NULL}
            };

            /* Lowercase the description for matching */
            char desc_lower[2048];
            for (int i = 0; description[i] && i < 2047; i++)
                desc_lower[i] = (description[i] >= 'A' && description[i] <= 'Z')
                    ? description[i] + 32 : description[i];
            desc_lower[strlen(description)] = 0;

            /* Match keywords → build node list */
            typedef struct { char id[64]; char label[64]; char proc[64];
                             char libs[128]; char runtime[32]; } synth_node_t;
            synth_node_t nodes[16];
            int node_count = 0;

            for (int k = 0; KWMAP[k].kw && node_count < 16; k++) {
                if (strstr(desc_lower, KWMAP[k].kw)) {
                    /* Check not already added */
                    int dup = 0;
                    for (int j = 0; j < node_count; j++)
                        if (strcmp(nodes[j].id, KWMAP[k].id) == 0) { dup = 1; break; }
                    if (dup) continue;
                    strncpy(nodes[node_count].id, KWMAP[k].id, 63);
                    strncpy(nodes[node_count].label, KWMAP[k].label, 63);
                    strncpy(nodes[node_count].proc, KWMAP[k].proc, 63);
                    strncpy(nodes[node_count].libs, KWMAP[k].libs, 127);
                    strncpy(nodes[node_count].runtime, KWMAP[k].runtime, 31);
                    node_count++;
                }
            }

            /* If no keywords matched, fall back to generic 3-stage pipeline */
            if (node_count == 0) {
                const char *fallback[][5] = {
                    {"input", "Input", "stream_reader", "io", "native"},
                    {"process", "Process", "data_proc", "core", "native"},
                    {"output", "Output", "egress", "io", "native"},
                };
                for (int i = 0; i < 3; i++) {
                    strncpy(nodes[i].id, fallback[i][0], 63);
                    strncpy(nodes[i].label, fallback[i][1], 63);
                    strncpy(nodes[i].proc, fallback[i][2], 63);
                    strncpy(nodes[i].libs, fallback[i][3], 127);
                    strncpy(nodes[i].runtime, fallback[i][4], 31);
                }
                node_count = 3;
            }

            /* Absorb the synthesis through the 3-ring cascade */
            char raw[512];
            snprintf(raw, sizeof(raw), "synthesize %d nodes from: %.200s", node_count, description);
            csos_photon_t ph = csos_organism_absorb(org, "workflow_synth", raw, PROTO_LLM);

            /* Build Mermaid spec */
            char mermaid[4096] = {0};
            int mp = 0;
            for (int i = 0; i < node_count; i++) {
                if (i > 0) mp += snprintf(mermaid + mp, sizeof(mermaid) - mp, " --> ");
                mp += snprintf(mermaid + mp, sizeof(mermaid) - mp,
                    "%s[%s]", nodes[i].id, nodes[i].label);
            }

            /* Auto-save synthesized version */
            {
                char vpath[512];
                snprintf(vpath, sizeof(vpath), "%s/.csos/sessions", org->root);
                mkdir(vpath, 0755);
                snprintf(vpath, sizeof(vpath), "%s/.csos/sessions/spec_versions.json", org->root);
                char *vexist = (char *)calloc(1, 32768);
                if (vexist) {
                    strcpy(vexist, "{\"versions\":[]}");
                    FILE *vf = fopen(vpath, "r");
                    if (vf) { fread(vexist, 1, 32767, vf); fclose(vf); }
                    char *arr_end = strstr(vexist, "]}");
                    if (arr_end) {
                        char *vnew = (char *)calloc(1, 32768);
                        if (vnew) {
                            int has_entries = (arr_end - vexist) > 15;
                            *arr_end = 0;
                            char esc_merm[2048] = {0};
                            size_t emi = 0;
                            for (size_t mi = 0; mermaid[mi] && emi < sizeof(esc_merm) - 4; mi++) {
                                if (mermaid[mi] == '"') { esc_merm[emi++] = '\\'; esc_merm[emi++] = '"'; }
                                else if (mermaid[mi] == '\\') { esc_merm[emi++] = '\\'; esc_merm[emi++] = '\\'; }
                                else if ((unsigned char)mermaid[mi] >= 32) { esc_merm[emi++] = mermaid[mi]; }
                            }
                            esc_merm[emi] = 0;
                            char esc_desc[1024] = {0};
                            size_t edi = 0;
                            for (size_t di = 0; description[di] && edi < sizeof(esc_desc) - 4; di++) {
                                if (description[di] == '"') { esc_desc[edi++] = '\\'; esc_desc[edi++] = '"'; }
                                else if (description[di] == '\\') { esc_desc[edi++] = '\\'; esc_desc[edi++] = '\\'; }
                                else if ((unsigned char)description[di] >= 32) { esc_desc[edi++] = description[di]; }
                            }
                            esc_desc[edi] = 0;
                            snprintf(vnew, 32768,
                                "%s%s{\"name\":\"%s\",\"spec\":\"%s\",\"mermaid\":\"%s\","
                                "\"timestamp\":%ld,\"type\":\"synthesized\"}]}",
                                vexist, has_entries ? "," : "",
                                wf_name, esc_desc, esc_merm, (long)time(NULL));
                            vf = fopen(vpath, "w");
                            if (vf) { fputs(vnew, vf); fclose(vf); }
                            free(vnew);
                        }
                    }
                    free(vexist);
                }
            }

            /* Build response with per-node metadata */
            int pos = snprintf(json_out, out_sz,
                "{\"synthesized\":true,\"mermaid\":\"%s\",\"nodes\":%d,\"stages\":[",
                mermaid, node_count);
            for (int i = 0; i < node_count && pos < (int)out_sz - 512; i++) {
                /* Motor strength for this node */
                uint32_t h = 0;
                for (const char *c = nodes[i].id; *c; c++) h = h * 31 + (uint8_t)*c;
                h = 1000 + (h % 9000);
                csos_membrane_t *d = _d;
                double strength = d ? csos_motor_strength(d, h) : 0;

                if (i > 0) pos += snprintf(json_out + pos, out_sz - pos, ",");
                pos += snprintf(json_out + pos, out_sz - pos,
                    "{\"id\":\"%s\",\"label\":\"%s\",\"processing_unit\":\"%s\","
                    "\"libs\":\"%s\",\"runtime\":\"%s\",\"hash\":%u,"
                    "\"motor_strength\":%.3f,\"state\":\"pending\"}",
                    nodes[i].id, nodes[i].label, nodes[i].proc,
                    nodes[i].libs, nodes[i].runtime, h, strength);
            }
            pos += snprintf(json_out + pos, out_sz - pos,
                "],\"delta\":%d,\"decision\":\"%s\"}",
                ph.delta, (const char*[]){"EXPLORE","EXECUTE","ASK","STORE"}[ph.decision & 3]);
            return 0;
        }

        /* ── JOBS: Track workflow executions persistently ── */
        if (strcmp(subaction, "jobs") == 0) {
            char job_action[32] = "list";
            json_str(json_in, "do", job_action, sizeof(job_action));

            char jpath[512];
            snprintf(jpath, sizeof(jpath), "%s/.csos/sessions/jobs.json", org->root);

            if (strcmp(job_action, "list") == 0) {
                FILE *f = fopen(jpath, "r");
                if (!f) { snprintf(json_out, out_sz, "{\"jobs\":[]}"); return 0; }
                char data[32768] = {0};
                fread(data, 1, sizeof(data)-1, f); fclose(f);
                snprintf(json_out, out_sz, "%s", data);
                return 0;
            }

            /* Record a job from a run result */
            if (strcmp(job_action, "record") == 0) {
                char jname[128] = {0}, jstatus[32] = {0}, jspec[2048] = {0};
                json_str(json_in, "name", jname, sizeof(jname));
                json_str(json_in, "status", jstatus, sizeof(jstatus));
                json_str(json_in, "spec", jspec, sizeof(jspec));
                if (!jname[0]) strncpy(jname, "unnamed", sizeof(jname));
                if (!jstatus[0]) strncpy(jstatus, "running", sizeof(jstatus));

                /* Read existing jobs */
                char existing[32768] = "{\"jobs\":[]}";
                FILE *f = fopen(jpath, "r");
                if (f) { fread(existing, 1, sizeof(existing)-1, f); fclose(f); }

                /* Find jobs array end and insert */
                char *arr_end = strstr(existing, "]}");
                if (arr_end) {
                    char newfile[32768];
                    int has_jobs = (arr_end - existing) > 10; /* has existing entries */
                    *arr_end = 0;
                    snprintf(newfile, sizeof(newfile),
                        "%s%s{\"name\":\"%s\",\"status\":\"%s\",\"spec\":\"%.*s\","
                        "\"cycle\":%u,\"timestamp\":%ld}]}",
                        existing, has_jobs ? "," : "",
                        jname, jstatus, 200, jspec,
                        _o
                            ? _o->cycles : 0,
                        (long)time(NULL));
                    char dir[512];
                    snprintf(dir, sizeof(dir), "%s/.csos/sessions", org->root);
                    mkdir(dir, 0755);
                    f = fopen(jpath, "w");
                    if (f) { fputs(newfile, f); fclose(f); }
                }

                snprintf(json_out, out_sz, "{\"recorded\":\"%s\",\"status\":\"%s\"}", jname, jstatus);
                return 0;
            }

            snprintf(json_out, out_sz, "{\"error\":\"unknown job action: %s (use list/record)\"}", job_action);
            return -1;
        }

        snprintf(json_out, out_sz, "{\"error\":\"unknown workflow sub: %s (use draft/run/run_step/configure/complete/synthesize/versions/restore/jobs)\"}", subaction);
        return -1;
    }

    /* ═══ AUTH: Source registration with authentication ladder ═══ */
    /*
     * Auth ladder — 3 levels of trust for external data sources:
     *   Level 0: none    — local substrates only (default)
     *   Level 1: api_key — registered with shared secret
     *   Level 2: token   — JWT bearer token
     *   Level 3: oauth   — full OAuth2 flow
     *
     * Each registered source becomes a substrate. Absorbed through physics.
     * Motor memory tracks source reliability over time (spaced rep).
     * High-strength sources get faster trust (Boyer gate).
     */
    if (strcmp(action, "auth") == 0) {
        char subaction[64] = {0}, source_name[128] = {0};
        char auth_level[32] = {0}, credential[512] = {0};
        json_str(json_in, "sub", subaction, sizeof(subaction));
        json_str(json_in, "source", source_name, sizeof(source_name));
        json_str(json_in, "level", auth_level, sizeof(auth_level));
        json_str(json_in, "credential", credential, sizeof(credential));

        /* ── REGISTER: Add authenticated source ── */
        if (strcmp(subaction, "register") == 0) {
            if (!source_name[0]) {
                snprintf(json_out, out_sz, "{\"error\":\"source name required\"}");
                return -1;
            }

            /* Determine auth level */
            int level = 0;
            if (strcmp(auth_level, "api_key") == 0) level = 1;
            else if (strcmp(auth_level, "token") == 0) level = 2;
            else if (strcmp(auth_level, "oauth") == 0) level = 3;

            /* Store source registration in sessions */
            char spath[512];
            snprintf(spath, sizeof(spath), "%s/.csos/sessions", org->root);
            mkdir(spath, 0755);
            snprintf(spath, sizeof(spath), "%s/.csos/sessions/sources.json", org->root);

            /* Read existing sources */
            char existing[16384] = "{}";
            FILE *f = fopen(spath, "r");
            if (f) { fread(existing, 1, sizeof(existing)-1, f); fclose(f); }

            /* Append source (simple JSON append) */
            char *end = strrchr(existing, '}');
            if (end) {
                char newfile[16384];
                *end = 0;
                size_t elen = strlen(existing);
                if (elen > 2)
                    snprintf(newfile, sizeof(newfile),
                        "%s,\n  \"%s\": {\"level\": %d, \"type\": \"%s\"}}",
                        existing, source_name, level, auth_level);
                else
                    snprintf(newfile, sizeof(newfile),
                        "{\n  \"%s\": {\"level\": %d, \"type\": \"%s\"}}",
                        source_name, level, auth_level);
                f = fopen(spath, "w");
                if (f) { fputs(newfile, f); fclose(f); }
            }

            /* Absorb registration event — physics tracks source reliability */
            char raw[256];
            snprintf(raw, sizeof(raw), "auth_register %s level=%d", source_name, level);
            csos_photon_t ph = csos_organism_absorb(org, source_name, raw, PROTO_LLM);

            snprintf(json_out, out_sz,
                "{\"registered\":\"%s\",\"auth_level\":%d,\"auth_type\":\"%s\","
                "\"motor_strength\":%.3f,\"delta\":%d,\"decision\":\"%s\"}",
                source_name, level, auth_level,
                ph.motor_strength, ph.delta,
                (const char*[]){"EXPLORE","EXECUTE","ASK","STORE"}[ph.decision & 3]);
            return 0;
        }

        /* ── LIST: Show registered sources ── */
        if (strcmp(subaction, "list") == 0) {
            char spath[512];
            snprintf(spath, sizeof(spath), "%s/.csos/sessions/sources.json", org->root);
            FILE *f = fopen(spath, "r");
            if (!f) {
                snprintf(json_out, out_sz, "{\"sources\":{},\"count\":0}");
                return 0;
            }
            char data[16384] = {0};
            size_t n = fread(data, 1, sizeof(data)-1, f);
            fclose(f);
            data[n] = 0;
            snprintf(json_out, out_sz, "{\"sources\":%s}", data);
            return 0;
        }

        /* ── CHECK: Verify a source's auth level ── */
        if (strcmp(subaction, "check") == 0) {
            if (!source_name[0]) {
                snprintf(json_out, out_sz, "{\"error\":\"source name required\"}");
                return -1;
            }
            /* Check motor strength — high strength = trusted source */
            csos_membrane_t *d = _d;
            uint32_t sh = 0;
            for (const char *c = source_name; *c; c++) sh = sh * 31 + (uint8_t)*c;
            sh = 1000 + (sh % 9000);
            double strength = d ? csos_motor_strength(d, sh) : 0;

            snprintf(json_out, out_sz,
                "{\"source\":\"%s\",\"hash\":%u,\"motor_strength\":%.3f,"
                "\"trusted\":%s}",
                source_name, sh, strength,
                strength > 0.5 ? "true" : "false");
            return 0;
        }

        snprintf(json_out, out_sz, "{\"error\":\"unknown auth sub: %s (use register/list/check)\"}", subaction);
        return -1;
    }

    /* ═══ CLUSTER: Workflow instances as observable units ═══ */
    /*
     * A cluster is a running workflow instance. Each node in the workflow
     * becomes a substrate in the cluster. The cluster tracks:
     *   - Per-node state (pending/running/done/failed)
     *   - Per-node compile spec (processing_unit, libs, runtime)
     *   - Aggregate physics (gradient, speed, decision) from the organism
     *   - Job history (persisted to .csos/sessions/clusters.json)
     *
     * Clusters are substrates. Motor memory tracks cluster health.
     * Boyer decides when a cluster needs attention (speed drops below rw).
     *
     * The agent creates clusters by running workflows. Users observe
     * cluster state through the canvas SSE stream or through
     * csos-core cluster=status.
     */
    if (strcmp(action, "cluster") == 0) {
        char subaction[64] = {0}, cluster_id[128] = {0};
        json_str(json_in, "sub", subaction, sizeof(subaction));
        json_str(json_in, "id", cluster_id, sizeof(cluster_id));

        char cpath[512];
        snprintf(cpath, sizeof(cpath), "%s/.csos/sessions", org->root);
        mkdir(cpath, 0755);
        snprintf(cpath, sizeof(cpath), "%s/.csos/sessions/clusters.json", org->root);

        /* ── CREATE: Instantiate a workflow as a cluster ── */
        if (strcmp(subaction, "create") == 0) {
            char wf_spec[4096] = {0};
            json_str(json_in, "spec", wf_spec, sizeof(wf_spec));
            if (!cluster_id[0] || !wf_spec[0]) {
                snprintf(json_out, out_sz, "{\"error\":\"id and spec required\"}");
                return -1;
            }

            /* Synthesize the spec to get per-node metadata */
            char synth_req[8192];
            snprintf(synth_req, sizeof(synth_req),
                "{\"action\":\"workflow\",\"sub\":\"synthesize\",\"description\":\"%s\"}", wf_spec);
            char synth_resp[16384] = {0};
            csos_handle(org, synth_req, synth_resp, sizeof(synth_resp));

            /* Absorb cluster creation */
            char raw[512];
            snprintf(raw, sizeof(raw), "cluster_create %s", cluster_id);
            csos_photon_t ph = csos_organism_absorb(org, cluster_id, raw, PROTO_LLM);

            /* Persist cluster record */
            char existing[32768] = "{\"clusters\":[]}";
            FILE *f = fopen(cpath, "r");
            if (f) { fread(existing, 1, sizeof(existing)-1, f); fclose(f); }
            char *arr_end = strstr(existing, "]}");
            if (arr_end) {
                char newfile[32768];
                int has = (arr_end - existing) > 14;
                *arr_end = 0;
                snprintf(newfile, sizeof(newfile),
                    "%s%s{\"id\":\"%s\",\"spec\":\"%.*s\",\"state\":\"running\","
                    "\"cycle\":%u,\"created\":%ld}]}",
                    existing, has ? "," : "",
                    cluster_id, 500, wf_spec,
                    csos_organism_find(org,"eco_organism")
                        ? csos_organism_find(org,"eco_organism")->cycles : 0,
                    (long)time(NULL));
                f = fopen(cpath, "w");
                if (f) { fputs(newfile, f); fclose(f); }
            }

            /* Return cluster + synthesized metadata */
            int pos = snprintf(json_out, out_sz,
                "{\"cluster\":\"%s\",\"state\":\"running\","
                "\"delta\":%d,\"decision\":\"%s\","
                "\"synthesis\":%s}",
                cluster_id, ph.delta,
                (const char*[]){"EXPLORE","EXECUTE","ASK","STORE"}[ph.decision & 3],
                synth_resp[0] ? synth_resp : "null");
            (void)pos;
            return 0;
        }

        /* ── STATUS: Check a specific cluster ── */
        if (strcmp(subaction, "status") == 0) {
            if (!cluster_id[0]) {
                snprintf(json_out, out_sz, "{\"error\":\"id required\"}");
                return -1;
            }
            /* Get motor strength for this cluster (how well-known it is) */
            uint32_t sh = 0;
            for (const char *c = cluster_id; *c; c++) sh = sh * 31 + (uint8_t)*c;
            sh = 1000 + (sh % 9000);
            csos_membrane_t *d = _d;
            csos_membrane_t *o = _o;
            double strength = d ? csos_motor_strength(d, sh) : 0;

            snprintf(json_out, out_sz,
                "{\"cluster\":\"%s\",\"hash\":%u,"
                "\"motor_strength\":%.3f,\"trusted\":%s,"
                "\"organism\":{\"speed\":%.3f,\"rw\":%.3f,\"decision\":\"%s\","
                "\"gradient\":%.0f,\"mode\":\"%s\"}}",
                cluster_id, sh, strength,
                strength > 0.5 ? "true" : "false",
                o ? o->speed : 0, o ? o->rw : 0,
                o ? (const char*[]){"EXPLORE","EXECUTE","ASK","STORE"}[o->decision] : "EXPLORE",
                o ? o->gradient : 0,
                o && o->mode == MODE_BUILD ? "build" : "plan");
            return 0;
        }

        /* ── LIST: All clusters ── */
        if (strcmp(subaction, "list") == 0) {
            FILE *f = fopen(cpath, "r");
            if (!f) { snprintf(json_out, out_sz, "{\"clusters\":[]}"); return 0; }
            char data[32768] = {0};
            fread(data, 1, sizeof(data)-1, f); fclose(f);
            snprintf(json_out, out_sz, "%s", data);
            return 0;
        }

        snprintf(json_out, out_sz, "{\"error\":\"unknown cluster sub: %s (use create/status/list)\"}", subaction);
        return -1;
    }

    /* ═══ SOURCE: Auto-validate and expose as wrapper objects ═══ */
    /*
     * Validates a registered source by probing it and returns a
     * typed wrapper object that can be referenced in workflow nodes.
     *
     * Wrapper object structure:
     *   { name, type, auth_level, motor_strength, validated,
     *     capabilities: [read, write, stream, ...],
     *     atom: { hash, spectral, resonated } }
     *
     * This wraps a source as a first-class atom object that workflow
     * nodes can bind to. The atom's motor_strength indicates reliability.
     */
    if (strcmp(action, "source") == 0) {
        char subaction[64] = {0}, source_name[128] = {0};
        json_str(json_in, "sub", subaction, sizeof(subaction));
        json_str(json_in, "name", source_name, sizeof(source_name));

        if (strcmp(subaction, "validate") == 0) {
            if (!source_name[0]) {
                snprintf(json_out, out_sz, "{\"error\":\"name required\"}");
                return -1;
            }

            /* Compute source hash and check motor memory */
            uint32_t sh = 0;
            for (const char *c = source_name; *c; c++) sh = sh * 31 + (uint8_t)*c;
            sh = 1000 + (sh % 9000);
            csos_membrane_t *d = _d;
            double strength = d ? csos_motor_strength(d, sh) : 0;

            /* Check if source is registered in auth */
            char spath[512];
            snprintf(spath, sizeof(spath), "%s/.csos/sessions/sources.json", org->root);
            int auth_level = 0;
            char auth_type[32] = "none";
            FILE *sf = fopen(spath, "r");
            if (sf) {
                char sdata[16384] = {0};
                fread(sdata, 1, sizeof(sdata)-1, sf); fclose(sf);
                /* Simple: check if source name appears */
                if (strstr(sdata, source_name)) {
                    char *lp = strstr(sdata, source_name);
                    if (lp) {
                        char *lvl = strstr(lp, "\"level\":");
                        if (lvl) auth_level = (int)strtol(lvl + 8, NULL, 10);
                        char *tp = strstr(lp, "\"type\":\"");
                        if (tp) {
                            tp += 8; int ti = 0;
                            while (*tp && *tp != '"' && ti < 31) auth_type[ti++] = *tp++;
                            auth_type[ti] = 0;
                        }
                    }
                }
            }

            /* Absorb validation probe through physics */
            char raw[256];
            snprintf(raw, sizeof(raw), "source_validate %s level=%d", source_name, auth_level);
            csos_photon_t ph = csos_organism_absorb(org, source_name, raw, PROTO_LLM);

            /* Infer capabilities from auth level */
            const char *caps = "read";
            if (auth_level >= 1) caps = "read,query";
            if (auth_level >= 2) caps = "read,query,write";
            if (auth_level >= 3) caps = "read,query,write,stream,admin";

            /* Return typed wrapper object */
            snprintf(json_out, out_sz,
                "{\"wrapper\":{"
                "\"name\":\"%s\","
                "\"type\":\"source_atom\","
                "\"auth_level\":%d,"
                "\"auth_type\":\"%s\","
                "\"motor_strength\":%.3f,"
                "\"validated\":%s,"
                "\"capabilities\":\"%s\","
                "\"atom\":{\"hash\":%u,\"resonated\":%s,\"delta\":%d},"
                "\"bind_as\":\"%s[%s]\""
                "}}",
                source_name, auth_level, auth_type,
                ph.motor_strength,
                ph.resonated ? "true" : "false",
                caps,
                sh, ph.resonated ? "true" : "false", ph.delta,
                source_name, source_name);
            return 0;
        }

        /* ── WRAPPERS: List all validated source wrapper objects ── */
        if (strcmp(subaction, "wrappers") == 0) {
            /* Read sources, generate wrapper for each */
            char spath[512];
            snprintf(spath, sizeof(spath), "%s/.csos/sessions/sources.json", org->root);
            FILE *sf = fopen(spath, "r");
            if (!sf) { snprintf(json_out, out_sz, "{\"wrappers\":[]}"); return 0; }
            char sdata[16384] = {0};
            fread(sdata, 1, sizeof(sdata)-1, sf); fclose(sf);

            csos_membrane_t *d = _d;
            int pos = snprintf(json_out, out_sz, "{\"wrappers\":[");
            int first = 1;

            /* Parse each source name from the JSON */
            char *p = sdata;
            while ((p = strchr(p, '"')) != NULL && pos < (int)out_sz - 256) {
                p++;
                /* Check if this looks like a source name (not a key like "level") */
                if (strncmp(p, "level", 5) == 0 || strncmp(p, "type", 4) == 0 ||
                    *p == '{' || *p == '}') { p++; continue; }

                char name[128] = {0}; int ni = 0;
                while (*p && *p != '"' && ni < 127) name[ni++] = *p++;
                name[ni] = 0;
                if (ni == 0 || *p != '"') continue;
                p++;

                /* Skip if it's a JSON value, not a key */
                while (*p && *p == ' ') p++;
                if (*p != ':') continue;

                /* Compute hash and motor strength */
                uint32_t sh = 0;
                for (const char *c = name; *c; c++) sh = sh * 31 + (uint8_t)*c;
                sh = 1000 + (sh % 9000);
                double strength = d ? csos_motor_strength(d, sh) : 0;

                if (!first) pos += snprintf(json_out + pos, out_sz - pos, ",");
                pos += snprintf(json_out + pos, out_sz - pos,
                    "{\"name\":\"%s\",\"hash\":%u,\"strength\":%.3f,\"bind\":\"%s[%s]\"}",
                    name, sh, strength, name, name);
                first = 0;
            }
            snprintf(json_out + pos, out_sz - pos, "]}");
            return 0;
        }

        snprintf(json_out, out_sz, "{\"error\":\"unknown source sub: %s (use validate/wrappers)\"}", subaction);
        return -1;
    }

    /* ═══ IR: Universal Intermediate Representation ═══ */
    /*
     * Generates a single runtime representation per query combining:
     *   1. SPEC layer — Mermaid DAG, atom definitions, edges, spectral config
     *   2. COMPILE layer — formula IR, param maps, optimization state
     *   3. RUNTIME layer — JIT pointers, motor state, ring physics, RDMA endpoints
     *
     * This is the Universal IR: one representation for Visual, Code, and Exec views.
     * Each view reads the layer it needs. Agents edit their respective layers.
     * LLVM JIT compiles spec → IR → native in one pass.
     */
    if (strcmp(action, "ir") == 0) {
        char ir_detail[32] = "full";
        char ir_spec[8192] = {0}, ir_name[128] = {0};
        json_str(json_in, "detail", ir_detail, sizeof(ir_detail));
        json_str(json_in, "spec", ir_spec, sizeof(ir_spec));
        json_str(json_in, "name", ir_name, sizeof(ir_name));

        csos_membrane_t *d = _d;
        csos_membrane_t *k = _k;
        csos_membrane_t *o = _o;

        int pos = snprintf(json_out, out_sz, "{\"ir\":true,\"layers\":{");

        /* ── SPEC LAYER: Visual view reads this ── */
        if (strcmp(ir_detail, "full") == 0 || strcmp(ir_detail, "spec") == 0) {
            pos += snprintf(json_out + pos, out_sz - pos, "\"spec\":{");
            /* Atoms from eco.csos */
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
            /* Rings */
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
            /* Mermaid spec if provided */
            if (ir_spec[0]) {
                pos += snprintf(json_out + pos, out_sz - pos, "\"mermaid\":\"%.*s\",", 2000, ir_spec);
            }
            pos += snprintf(json_out + pos, out_sz - pos, "\"foundation_atoms\":5,\"ring_count\":%d}", org->count);
        }

        /* ── COMPILE LAYER: Code view reads this ── */
        if (strcmp(ir_detail, "full") == 0 || strcmp(ir_detail, "compile") == 0) {
            if (strcmp(ir_detail, "full") == 0) pos += snprintf(json_out + pos, out_sz - pos, ",");
            pos += snprintf(json_out + pos, out_sz - pos, "\"compile\":{");
            /* Per-atom compile info */
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
                        if (pv != pv) pv = 0; /* Guard NaN */
                        pos += snprintf(json_out + pos, out_sz - pos,
                            "{\"key\":\"%s\",\"value\":%.6f}",
                            d->atoms[i].param_keys[j], pv);
                    }
                    pos += snprintf(json_out + pos, out_sz - pos, "]}");
                }
            }
            pos += snprintf(json_out + pos, out_sz - pos, "],");
            /* JIT state */
            int jit_on = 0, jit_atoms = 0;
#ifdef CSOS_HAS_LLVM
            jit_on = csos_jit_active();
            jit_atoms = csos_jit_atom_count();
#endif
            pos += snprintf(json_out + pos, out_sz - pos,
                "\"jit\":{\"enabled\":%s,\"compiled_atoms\":%d,\"opt_level\":\"O2\"},",
                jit_on ? "true" : "false", jit_atoms);
            /* Constants from membrane.h */
            pos += snprintf(json_out + pos, out_sz - pos,
                "\"constants\":{\"boyer_threshold\":%.3f,\"motor_growth\":%.3f,"
                "\"motor_decay_floor\":%.3f,\"motor_decay_ceil\":%.3f,"
                "\"calvin_freq_min\":%d,\"calvin_freq_max\":%d,"
                "\"forster_exp\":%d,"
                "\"rw_floor\":%.4f,\"rw_ceil\":%.4f,\"error_guard\":%.4f}}",
                CSOS_BOYER_THRESHOLD, CSOS_MOTOR_GROWTH,
                CSOS_MOTOR_DECAY_FLOOR, CSOS_MOTOR_DECAY_CEIL,
                CSOS_CALVIN_FREQ_MIN, CSOS_CALVIN_FREQ_MAX,
                CSOS_FORSTER_EXPONENT,
                CSOS_RW_FLOOR, CSOS_RW_CEIL, CSOS_ERROR_DENOM_GUARD);
        }

        /* ── RUNTIME LAYER: Exec view reads this ── */
        if (strcmp(ir_detail, "full") == 0 || strcmp(ir_detail, "runtime") == 0) {
            if (strcmp(ir_detail, "full") == 0 || strcmp(ir_detail, "compile") == 0)
                pos += snprintf(json_out + pos, out_sz - pos, ",");
            pos += snprintf(json_out + pos, out_sz - pos, "\"runtime\":{");
            /* Per-ring physics state */
            pos += snprintf(json_out + pos, out_sz - pos, "\"rings\":[");
            for (int i = 0; i < org->count && pos < (int)out_sz - 512; i++) {
                csos_membrane_t *m = org->membranes[i];
                if (i > 0) pos += snprintf(json_out + pos, out_sz - pos, ",");
                pos += snprintf(json_out + pos, out_sz - pos,
                    "{\"name\":\"%s\",\"gradient\":%.1f,\"speed\":%.6f,"
                    "\"F\":%.6f,\"rw\":%.6f,\"action_ratio\":%.6f,"
                    "\"decision\":\"%s\",\"mode\":\"%s\","
                    "\"cycles\":%u,\"motor_count\":%d,"
                    "\"mitchell_n\":%d,\"rdma\":%s,"
                    "\"couplings\":%d}",
                    m->name, m->gradient, m->speed, m->F, m->rw, m->action_ratio,
                    (const char*[]){"EXPLORE","EXECUTE","ASK","STORE"}[m->decision & 3],
                    m->mode == MODE_BUILD ? "build" : "plan",
                    m->cycles, m->motor_count, m->mitchell_n,
                    m->rdma_enabled ? "true" : "false",
                    m->coupling_count);
            }
            pos += snprintf(json_out + pos, out_sz - pos, "],");
            /* Motor memory top entries */
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
            pos += snprintf(json_out + pos, out_sz - pos, "],");
            /* RDMA endpoints */
            pos += snprintf(json_out + pos, out_sz - pos, "\"rdma\":{\"enabled\":");
            int rdma_any = 0;
            for (int i = 0; i < org->count; i++)
                if (org->membranes[i]->rdma_enabled) rdma_any = 1;
            pos += snprintf(json_out + pos, out_sz - pos,
                "%s,\"endpoints\":[", rdma_any ? "true" : "false");
            int rdma_first = 1;
            for (int i = 0; i < org->count && pos < (int)out_sz - 256; i++) {
                csos_membrane_t *m = org->membranes[i];
                if (!m->rdma_enabled) continue;
                if (!rdma_first) pos += snprintf(json_out + pos, out_sz - pos, ",");
                pos += snprintf(json_out + pos, out_sz - pos,
                    "{\"ring\":\"%s\",\"rkey\":%u,\"addr\":%llu}",
                    m->name, m->rdma_rkey, (unsigned long long)m->rdma_remote_addr);
                rdma_first = 0;
            }
            pos += snprintf(json_out + pos, out_sz - pos, "]}}");
        }

        pos += snprintf(json_out + pos, out_sz - pos, "}}");
        return 0;
    }

    /* ═══ RDMA: Enable remote direct memory access for cross-node coupling ═══ */
    if (strcmp(action, "rdma") == 0) {
        char subaction[64] = {0};
        json_str(json_in, "sub", subaction, sizeof(subaction));
        json_str(json_in, "ring", ring_name, sizeof(ring_name));

        if (strcmp(subaction, "register") == 0) {
            csos_membrane_t *m = ring_name[0] ?
                csos_organism_find(org, ring_name) : _o;
            if (!m) {
                snprintf(json_out, out_sz, "{\"error\":\"ring not found\"}");
                return -1;
            }
            csos_membrane_rdma_register(m);
            snprintf(json_out, out_sz,
                "{\"rdma_registered\":\"%s\",\"rkey\":%u,\"addr\":%llu}",
                m->name, m->rdma_rkey, (unsigned long long)m->rdma_remote_addr);
            return 0;
        }

        if (strcmp(subaction, "diffuse") == 0) {
            char remote_ring[128] = {0}, node_str[32] = {0};
            json_str(json_in, "remote_ring", remote_ring, sizeof(remote_ring));
            json_str(json_in, "node", node_str, sizeof(node_str));
            uint32_t node_id = node_str[0] ? (uint32_t)strtoul(node_str, NULL, 10) : 0;

            csos_membrane_t *m = ring_name[0] ?
                csos_organism_find(org, ring_name) : _o;
            if (!m || !remote_ring[0]) {
                snprintf(json_out, out_sz, "{\"error\":\"ring and remote_ring required\"}");
                return -1;
            }
            int result = csos_membrane_rdma_diffuse(m, remote_ring, node_id);
            snprintf(json_out, out_sz,
                "{\"rdma_diffuse\":\"%s\",\"remote\":\"%s\",\"node\":%u,"
                "\"transferred\":%d,\"coupling_count\":%d}",
                m->name, remote_ring, node_id, result, m->coupling_count);
            return 0;
        }

        if (strcmp(subaction, "status") == 0) {
            int pos = snprintf(json_out, out_sz, "{\"rdma_status\":[");
            for (int i = 0; i < org->count && pos < (int)out_sz - 256; i++) {
                csos_membrane_t *m = org->membranes[i];
                if (i > 0) pos += snprintf(json_out + pos, out_sz - pos, ",");
                pos += snprintf(json_out + pos, out_sz - pos,
                    "{\"ring\":\"%s\",\"enabled\":%s,\"rkey\":%u,"
                    "\"couplings\":%d}",
                    m->name, m->rdma_enabled ? "true" : "false",
                    m->rdma_rkey, m->coupling_count);
            }
            snprintf(json_out + pos, out_sz - pos, "]}");
            return 0;
        }

        snprintf(json_out, out_sz, "{\"error\":\"unknown rdma sub: %s (use register/diffuse/status)\"}", subaction);
        return -1;
    }

    /* ═══ COMPACT: Self-healing bloat removal (the system drives the change) ═══ */
    /*
     * This action enforces the Universal IR constraint across the entire tree:
     *   - specs/: Only eco.csos survives. Non-foundation atoms stripped.
     *   - .csos/deliveries/: Only photon-format JSON survives. Prose removed.
     *   - .canvas-tui/: Only index.html survives. Test files removed.
     *
     * Triggered by: agent action, --seed, or periodic (every 100 cycles).
     * This is NOT a one-time cleanup. It runs continuously.
     */
    if (strcmp(action, "compact") == 0) {
        int specs_cleaned = csos_compact_specs(org->root);
        int deliveries_cleaned = csos_compact_deliveries(org->root);
        int canvas_cleaned = csos_compact_canvas(org->root);
        int total = specs_cleaned + deliveries_cleaned + canvas_cleaned;

        /* Absorb the compact event into physics (the system observes itself) */
        char raw[256];
        snprintf(raw, sizeof(raw), "compact removed %d artifacts", total);
        csos_photon_t ph = csos_organism_absorb(org, "self_healing", raw, PROTO_INTERNAL);

        snprintf(json_out, out_sz,
            "{\"compacted\":true,\"specs_removed\":%d,\"deliveries_removed\":%d,"
            "\"canvas_removed\":%d,\"total\":%d,\"delta\":%d,\"decision\":\"%s\"}",
            specs_cleaned, deliveries_cleaned, canvas_cleaned, total,
            ph.delta, (const char*[]){"EXPLORE","EXECUTE","ASK","STORE"}[ph.decision & 3]);
        return 0;
    }

    /* ═══ VALIDATE: Check a spec file against foundation IR ═══ */
    /*
     * Note: csos_spec_parse() already runs IR validation on load.
     * This action reports what parse-time validation found.
     * The valid_atoms count is what survived; rejected is what was stripped.
     */
    if (strcmp(action, "validate") == 0) {
        char filepath[512] = {0};
        json_str(json_in, "path", filepath, sizeof(filepath));
        if (!filepath[0]) strncpy(filepath, "specs/eco.csos", sizeof(filepath));

        /* Parse includes auto-validation (strips non-foundation atoms).
         * The spec returned already has only valid atoms. */
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

/* ═══ HTTP PROTOCOL: Full daemon (Replit-style single binary) ═══ */
/*
 * One binary serves everything:
 *   GET  /              → canvas HTML (.canvas-tui/index.html)
 *   GET  /api/state     → full organism state as JSON
 *   GET  /api/templates → living template catalog
 *   GET  /events        → SSE stream (Server-Sent Events)
 *   POST /api/command   → JSON command → csos_handle() → JSON response
 *   OPTIONS *           → CORS preflight
 */

/* SSE client list (simple array, max 32 concurrent) */
#define MAX_SSE_CLIENTS 32
static int _sse_fds[MAX_SSE_CLIENTS];
static int _sse_count = 0;

#include <fcntl.h>
#include <errno.h>

/* Set socket to non-blocking mode.
 * Prevents slow SSE clients from stalling the main absorb() loop.
 * Without this, a single slow client blocks ALL physics processing. */
static void set_nonblocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags >= 0) fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

/* SSE broadcast with backpressure handling.
 * Non-blocking write: if a client's kernel buffer is full (EAGAIN),
 * drop the message for that client rather than blocking the main thread.
 * Dead/stalled clients are evicted immediately. */
static void sse_broadcast(csos_organism_t *org, const char *event, const char *data) {
    (void)org;
    char msg[8192];
    int len = snprintf(msg, sizeof(msg), "event: %s\ndata: %s\n\n", event, data);
    for (int i = _sse_count - 1; i >= 0; i--) {
        ssize_t written = write(_sse_fds[i], msg, len);
        if (written < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                /* Client buffer full — drop this message, keep client alive.
                 * The next state event will catch them up. */
                continue;
            }
            /* Client dead — evict */
            close(_sse_fds[i]);
            _sse_fds[i] = _sse_fds[--_sse_count];
        } else if (written < len) {
            /* Partial write — client too slow, evict to protect main thread */
            close(_sse_fds[i]);
            _sse_fds[i] = _sse_fds[--_sse_count];
        }
    }
}

static void http_send(int fd, int status, const char *ctype, const char *body, size_t blen) {
    /* Single writev-style send: header + body in one syscall */
    char hdr[512];
    int hlen = snprintf(hdr, sizeof(hdr),
        "HTTP/1.1 %d OK\r\nContent-Type: %s\r\nContent-Length: %zu\r\n"
        "Access-Control-Allow-Origin: *\r\nConnection: close\r\n\r\n",
        status, ctype, blen);
    /* Combine header + body into one write to avoid Nagle delays */
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

/* Cached canvas HTML — loaded once, served from memory.
 * Eliminates fopen/fseek/fread/malloc/free per request. */
/* Always read fresh from disk — no cache. Enables hot-reload during development. */
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
    /* No-cache headers prevent browser from caching stale HTML */
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

static void http_send_file(int fd, const char *path, const char *ctype) {
    FILE *f = fopen(path, "r");
    if (!f) { http_send(fd, 404, "text/plain", "not found", 9); return; }
    fseek(f, 0, SEEK_END); long sz = ftell(f); fseek(f, 0, SEEK_SET);
    char *buf = (char *)malloc(sz + 1);
    fread(buf, 1, sz, f); buf[sz] = 0; fclose(f);
    http_send(fd, 200, ctype, buf, sz);
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
            "\"rdma_enabled\":%d,\"coupling_count\":%d,"
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
            m->gradient_gap, m->rdma_enabled, m->coupling_count,
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
    /* Sessions as living equations */
    const char *stage_nm[] = {"seed","sprout","grow","bloom","harvest","dormant"};
    pos += snprintf(out + pos, sz - pos,
        "},\"organism_vitality\":%.6f,\"sessions\":[", org_vitality);
    for (int i = 0; i < org->session_count && pos < (int)sz - 512; i++) {
        const csos_session_t *s = &org->sessions[i];
        if (i > 0) pos += snprintf(out + pos, sz - pos, ",");
        pos += snprintf(out + pos, sz - pos,
            "{\"id\":\"%s\",\"substrate\":\"%s\",\"binding\":\"%s\","
            "\"stage\":\"%s\",\"vitality\":%.4f,\"trend\":%.4f,"
            "\"gradient\":%.0f,\"deliveries\":%d,\"ticks\":%d,"
            "\"autonomous\":%s,\"interval\":%d,"
            "\"ingress\":\"%s\",\"egress\":\"%s\"}",
            s->id, s->substrate, s->binding,
            stage_nm[s->stage < 6 ? s->stage : 0],
            s->vitality, s->vitality_trend,
            s->peak_gradient, s->deliveries, s->schedule.tick_count,
            s->schedule.autonomous ? "true" : "false",
            s->schedule.interval_secs,
            s->ingress.type, s->egress.type);
    }
    /* Recent events (bottlenecks, milestones) */
    const char *etype_n[] = {"agent","canvas","bottleneck","resolution","scheduler","milestone"};
    pos += snprintf(out + pos, sz - pos, "],\"events\":[");
    int ecount = org->event_count < 10 ? org->event_count : 10;
    for (int ei = 0; ei < ecount && pos < (int)sz - 256; ei++) {
        int eidx = (org->event_head - ecount + ei);
        if (eidx < 0) eidx += CSOS_MAX_EVENTS;
        eidx = eidx % CSOS_MAX_EVENTS;
        const csos_event_t *ev = &org->events[eidx];
        if (ei > 0) pos += snprintf(out + pos, sz - pos, ",");
        pos += snprintf(out + pos, sz - pos,
            "{\"type\":\"%s\",\"source\":\"%s\",\"session\":\"%s\","
            "\"message\":\"%s\",\"severity\":%d,\"vitality\":%.3f}",
            etype_n[ev->type < 6 ? ev->type : 0], ev->source,
            ev->session, ev->message, ev->severity, ev->vitality);
    }
    pos += snprintf(out + pos, sz - pos,
        "],\"clients\":%d,\"native\":true}", _sse_count);
    return pos;
}

/* Build template catalog JSON — each template includes Mermaid spec + per-node metadata */
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
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK); /* 127.0.0.1 — bypasses macOS firewall */
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
        /* Also poll SSE clients for disconnect detection */
        for (int i = 0; i < _sse_count; i++) {
            pfds[i + 1].fd = _sse_fds[i]; pfds[i + 1].events = POLLIN;
        }
        /* Poll with 1-second timeout (scheduler needs sub-second resolution
         * for interval_secs=1 sessions, but 1s is sufficient for most use) */
        int nfds = poll(pfds, 1 + _sse_count, 1000);

        /* ═══ SCHEDULER: Tick all due sessions (circadian rhythm) ═══
         * Every poll cycle (1s), check if any sessions are due.
         * This IS the heartbeat of the organism — sessions run themselves.
         * The scheduler is non-blocking: tick_all only runs ingress for
         * sessions whose next_tick <= now. */
        {
            int ticked = csos_session_tick_all(org);
            if (ticked > 0) {
                /* Log scheduler event */
                char emsg[128];
                snprintf(emsg, sizeof(emsg), "Ticked %d sessions autonomously", ticked);
                csos_event_log(org, EVT_SCHEDULER, "scheduler", "", emsg, 0);
                /* Broadcast tick + events to SSE clients */
                char tick_json[8192];
                int tp = snprintf(tick_json, sizeof(tick_json),
                    "{\"scheduler_tick\":true,\"sessions_ticked\":%d,", ticked);
                /* Include recent events in broadcast */
                tp += snprintf(tick_json + tp, sizeof(tick_json) - tp, "\"recent_events\":[");
                int ecount = org->event_count < 5 ? org->event_count : 5;
                const char *etype[] = {"agent","canvas","bottleneck","resolution","scheduler","milestone"};
                for (int ei = 0; ei < ecount && tp < (int)sizeof(tick_json) - 256; ei++) {
                    int eidx = (org->event_head - ecount + ei);
                    if (eidx < 0) eidx += CSOS_MAX_EVENTS;
                    eidx = eidx % CSOS_MAX_EVENTS;
                    const csos_event_t *ev = &org->events[eidx];
                    if (ei > 0) tp += snprintf(tick_json + tp, sizeof(tick_json) - tp, ",");
                    tp += snprintf(tick_json + tp, sizeof(tick_json) - tp,
                        "{\"type\":\"%s\",\"source\":\"%s\",\"session\":\"%s\","
                        "\"message\":\"%s\",\"severity\":%d}",
                        etype[ev->type < 6 ? ev->type : 0], ev->source,
                        ev->session, ev->message, ev->severity);
                }
                tp += snprintf(tick_json + tp, sizeof(tick_json) - tp, "]}");
                sse_broadcast(org, "response", tick_json);
                /* Auto-save after autonomous ticks */
                csos_organism_save(org);
            }
        }

        if (nfds <= 0) {
            /* Keepalive for SSE clients (non-blocking safe) */
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

        /* Parse HTTP method and path */
        char method[8] = {0}, path[256] = {0};
        sscanf(req, "%7s %255s", method, path);

        /* ── OPTIONS: CORS preflight ── */
        if (strcmp(method, "OPTIONS") == 0) {
            const char *cors = "HTTP/1.1 200 OK\r\nAccess-Control-Allow-Origin: *\r\n"
                "Access-Control-Allow-Methods: GET, POST, OPTIONS\r\n"
                "Access-Control-Allow-Headers: Content-Type\r\nContent-Length: 0\r\n"
                "Connection: close\r\n\r\n";
            write(cl, cors, strlen(cors));
            close(cl); continue;
        }

        /* ── GET routes ── */
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
                /* SSE: keep connection open, add to client list */
                const char *sse_hdr =
                    "HTTP/1.1 200 OK\r\nContent-Type: text/event-stream\r\n"
                    "Cache-Control: no-cache\r\nAccess-Control-Allow-Origin: *\r\n"
                    "Connection: keep-alive\r\n\r\n";
                write(cl, sse_hdr, strlen(sse_hdr));
                /* Send initial state */
                build_state_json(org, resp, sizeof(resp));
                char init[65536];
                int ilen = snprintf(init, sizeof(init), "event: state\ndata: %s\n\n", resp);
                write(cl, init, ilen);
                /* Add to SSE list (don't close).
                 * Set non-blocking so broadcast never stalls on slow clients. */
                if (_sse_count < MAX_SSE_CLIENTS) {
                    set_nonblocking(cl);
                    _sse_fds[_sse_count++] = cl;
                } else {
                    close(cl);
                }
                continue; /* Don't close — SSE connection stays open */
            }
            else {
                http_send(cl, 404, "text/plain", "not found", 9);
            }
            close(cl); continue;
        }

        /* ── POST routes ── */
        if (strcmp(method, "POST") == 0) {
            char *body = strstr(req, "\r\n\r\n");
            if (body) body += 4; else body = "{}";
            if (strcmp(path, "/api/command") == 0) {
                csos_handle(org, body, resp, sizeof(resp));
                http_send(cl, 200, "application/json", resp, strlen(resp));
                /* Broadcast to SSE clients */
                sse_broadcast(org, "response", resp);
            }
            else if (strcmp(path, "/api/agent") == 0) {
                /* ═══ MULTI-AGENT PROXY (async) ═══
                 * Routes to the right agent based on intent or explicit "agent" field.
                 * 4 agents: csos-living (orchestrator), csos-observer (read),
                 *           csos-operator (write), csos-analyst (patterns).
                 * Canvas sends {message, agent?} → we fork → opencode run → SSE back. */
                char msg[4096] = {0}, agent_name[64] = {0}, sess_ctx[CSOS_NAME_LEN] = {0};
                json_str(body, "message", msg, sizeof(msg));
                json_str(body, "agent", agent_name, sizeof(agent_name));
                json_str(body, "session", sess_ctx, sizeof(sess_ctx));
                if (!msg[0]) {
                    http_send(cl, 400, "application/json",
                        "{\"error\":\"message required\"}", 28);
                } else {
                    /* ═══ SESSION CONTEXT INJECTION ═══
                     * When Canvas sends a session field, enrich the message
                     * AND log the event with session context so agents
                     * can be session-aware. */
                    if (sess_ctx[0]) {
                        csos_event_log(org, EVT_CANVAS_ACTION, "canvas",
                            sess_ctx, msg, 0);
                        /* Absorb attention into cockpit for this session */
                        if (_k) {
                            csos_session_t *s = csos_session_find(org, sess_ctx);
                            if (s) {
                                csos_membrane_absorb(_k, 15.0, s->substrate_hash, PROTO_STDIO);
                            }
                        }
                    }
                    /* Auto-route if no agent specified */
                    if (!agent_name[0]) {
                        /* Intent detection: keywords → agent */
                        const char *low = msg; /* Simple keyword check */
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
                    if (!sess_ctx[0]) {
                        csos_event_log(org, EVT_CANVAS_ACTION, "canvas", "",
                            msg, 0);
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
                            size_t n = fread(out+total, 1, sizeof(out)-1-total, fp);
                            if (n == 0) break;
                            total += n;
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
                        /* POST result back via msg channel (with session context) */
                        char post[32768];
                        int plen = snprintf(post, sizeof(post),
                            "{\"action\":\"msg\",\"sub\":\"send\",\"from\":\"%s\","
                            "\"to\":\"canvas\",\"session\":\"%s\",\"body\":\"%.*s\"}",
                            agent_name, sess_ctx, (int)(sizeof(post)-300), clean);
                        char curl_cmd[200];
                        snprintf(curl_cmd, sizeof(curl_cmd),
                            "curl -sf -X POST http://127.0.0.1:4200/api/command "
                            "-H 'Content-Type: application/json' -d @-");
                        FILE *cp = popen(curl_cmd, "w");
                        if (cp) { fwrite(post, 1, plen, cp); pclose(cp); }
                        _exit(0);
                    }
                    /* Parent: respond immediately */
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
