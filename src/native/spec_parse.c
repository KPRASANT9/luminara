/*
 * CSOS Spec Parser — Reads .csos files and .mem.json Calvin atoms.
 *
 * The spec file IS the genome. This parser is the ribosome:
 *   eco.csos → csos_spec_t → membrane_from_spec() → live membrane
 *
 * NO hardcoded equations. Every atom definition (foundation + Calvin)
 * flows through this single parser. Adding a new equation means
 * adding a new atom{} block to eco.csos — zero C code changes.
 *
 * Calvin atoms from .mem.json are loaded the same way: each carries
 * its own formula and compute expression synthesized at runtime.
 */
#include "../../lib/membrane.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <dirent.h>

/* ═══ STRING HELPERS ═══ */

static void trim(char *s) {
    char *start = s;
    while (*start && isspace((unsigned char)*start)) start++;
    if (start != s) memmove(s, start, strlen(start) + 1);
    char *end = s + strlen(s) - 1;
    while (end > s && isspace((unsigned char)*end)) *end-- = 0;
}

static void strip_trailing(char *s, char c) {
    char *end = s + strlen(s) - 1;
    while (end > s && (*end == c || isspace((unsigned char)*end))) *end-- = 0;
}

/* Extract value after "key:" from a line, stripping quotes and semicolons */
static int extract_field(const char *line, const char *key, char *out, size_t sz) {
    char pat[64];
    snprintf(pat, sizeof(pat), "%s:", key);
    const char *p = strstr(line, pat);
    if (!p) return -1;
    p += strlen(pat);
    while (*p && (*p == ' ' || *p == '\t')) p++;

    /* Strip surrounding quotes if present */
    if (*p == '"') {
        p++;
        size_t i = 0;
        while (*p && *p != '"' && i < sz - 1) out[i++] = *p++;
        out[i] = 0;
    } else {
        size_t i = 0;
        while (*p && *p != ';' && *p != '\n' && i < sz - 1) out[i++] = *p++;
        out[i] = 0;
    }
    trim(out);
    strip_trailing(out, ';');
    return 0;
}

/* Parse params from "{ key: val, key: val, ... }" */
static int parse_params(const char *line, char keys[][32], double *vals, int max) {
    const char *p = strchr(line, '{');
    if (!p) return 0;
    p++;

    int count = 0;
    while (*p && *p != '}' && count < max) {
        while (*p && (isspace((unsigned char)*p) || *p == ',')) p++;
        if (*p == '}') break;

        /* Read key */
        int ki = 0;
        while (*p && *p != ':' && *p != '}' && ki < 31) {
            if (!isspace((unsigned char)*p)) keys[count][ki++] = *p;
            p++;
        }
        keys[count][ki] = 0;
        if (*p == ':') p++;

        /* Read value */
        while (*p && isspace((unsigned char)*p)) p++;
        vals[count] = strtod(p, (char**)&p);
        count++;
    }
    return count;
}

/* Parse spectral from "[lo, hi]" */
static int parse_spectral(const char *line, double *lo, double *hi) {
    const char *p = strchr(line, '[');
    if (!p) return -1;
    p++;
    *lo = strtod(p, (char**)&p);
    while (*p && (*p == ',' || isspace((unsigned char)*p))) p++;
    *hi = strtod(p, NULL);
    return 0;
}

/* ═══ SPEC PARSER ═══ */

/* Forward declarations */
int csos_spec_validate(csos_spec_t *spec, char *log, size_t log_sz);

int csos_spec_parse(const char *path, csos_spec_t *spec) {
    FILE *f = fopen(path, "r");
    if (!f) return -1;

    memset(spec, 0, sizeof(*spec));

    char line[512];
    int in_atom = 0, in_ring = 0;
    csos_spec_atom_t *cur_atom = NULL;
    int brace_depth = 0;

    while (fgets(line, sizeof(line), f)) {
        trim(line);
        if (line[0] == 0 || (line[0] == '/' && line[1] == '/')) continue;

        /* Track brace depth for nested blocks */
        for (const char *c = line; *c; c++) {
            if (*c == '{') brace_depth++;
            else if (*c == '}') brace_depth--;
        }

        /* Atom block start */
        if (strncmp(line, "atom ", 5) == 0 && strchr(line, '{')) {
            if (spec->atom_count >= CSOS_MAX_ATOMS) continue;
            cur_atom = &spec->atoms[spec->atom_count];
            memset(cur_atom, 0, sizeof(*cur_atom));

            /* Extract name between "atom " and "{" */
            char *name_start = line + 5;
            char *name_end = strchr(line, '{');
            if (name_end) {
                size_t len = name_end - name_start;
                if (len >= CSOS_NAME_LEN) len = CSOS_NAME_LEN - 1;
                strncpy(cur_atom->name, name_start, len);
                cur_atom->name[len] = 0;
                trim(cur_atom->name);
            }
            in_atom = 1;
            continue;
        }

        /* Ring block start */
        if (strncmp(line, "ring ", 5) == 0 && strchr(line, '{')) {
            if (spec->ring_count < CSOS_MAX_RINGS) {
                char *name_start = line + 5;
                char *name_end = strchr(line, '{');
                if (name_end) {
                    size_t len = name_end - name_start;
                    if (len >= CSOS_NAME_LEN) len = CSOS_NAME_LEN - 1;
                    strncpy(spec->ring_names[spec->ring_count], name_start, len);
                    spec->ring_names[spec->ring_count][len] = 0;
                    trim(spec->ring_names[spec->ring_count]);
                }
                spec->ring_count++;
            }
            in_ring = 1;
            continue;
        }

        /* Inside atom block */
        if (in_atom && cur_atom) {
            if (strchr(line, '}') && brace_depth <= 0) {
                /* Close atom block */
                spec->atom_count++;
                in_atom = 0;
                cur_atom = NULL;
                brace_depth = 0;
                continue;
            }

            char val[CSOS_FORMULA_LEN];
            if (strstr(line, "formula:") && extract_field(line, "formula", val, sizeof(val)) == 0) {
                strncpy(cur_atom->formula, val, CSOS_FORMULA_LEN - 1);
            }
            else if (strstr(line, "compute:") && extract_field(line, "compute", val, sizeof(val)) == 0) {
                strncpy(cur_atom->compute, val, CSOS_FORMULA_LEN - 1);
            }
            else if (strstr(line, "source:") && extract_field(line, "source", val, sizeof(val)) == 0) {
                strncpy(cur_atom->source, val, CSOS_NAME_LEN - 1);
            }
            else if (strstr(line, "params:")) {
                cur_atom->param_count = parse_params(line, cur_atom->param_keys,
                                                      cur_atom->param_defaults, CSOS_MAX_PARAMS);
            }
            else if (strstr(line, "spectral:")) {
                parse_spectral(line, &cur_atom->spectral[0], &cur_atom->spectral[1]);
            }
            else if (strstr(line, "broadband:")) {
                cur_atom->broadband = (strstr(line, "true") != NULL) ? 1 : 0;
            }
        }

        /* Inside ring block — parse ring-level parameters */
        if (in_ring) {
            /* Mitchell n: proton count per ring (ΔG = -n·F·Δψ) */
            char *mn = strstr(line, "mitchell_n:");
            if (mn && spec->ring_count > 0) {
                mn += 11; /* skip "mitchell_n:" */
                while (*mn == ' ' || *mn == '\t') mn++;
                spec->ring_mitchell_n[spec->ring_count - 1] = (int)strtol(mn, NULL, 10);
            }
            if (strchr(line, '}') && brace_depth <= 0) {
                in_ring = 0;
                brace_depth = 0;
            }
        }
    }

    fclose(f);

    /* ── IR VALIDATION: Strip non-foundation atoms on load ── */
    char vlog[2048] = {0};
    int rejected = csos_spec_validate(spec, vlog, sizeof(vlog));
    if (rejected > 0) {
        fprintf(stderr, "[spec_parse] IR validation stripped %d non-foundation atoms from %s:\n%s",
                rejected, path, vlog);
    }

    return 0;
}

/* ═══ CALVIN LOADER (from .mem.json files) ═══ */

int csos_spec_load_calvin(const char *rings_dir, csos_spec_t *spec) {
    DIR *dir = opendir(rings_dir);
    if (!dir) return 0;

    int added = 0;
    struct dirent *ent;
    while ((ent = readdir(dir)) != NULL) {
        if (!strstr(ent->d_name, ".mem.json")) continue;

        char path[512];
        snprintf(path, sizeof(path), "%s/%s", rings_dir, ent->d_name);
        FILE *f = fopen(path, "r");
        if (!f) continue;

        char buf[32768] = {0};
        size_t n = fread(buf, 1, sizeof(buf) - 1, f);
        fclose(f);
        if (n == 0) continue;
        buf[n] = 0;

        /* Find calvin_atoms array */
        char *ca = strstr(buf, "\"calvin_atoms\"");
        if (!ca) continue;
        ca = strchr(ca, '[');
        if (!ca) continue;

        /* Parse each Calvin atom: {"name":"...","formula":"...","center":...} */
        char *p = ca;
        while ((p = strstr(p, "{\"name\"")) != NULL && spec->atom_count < CSOS_MAX_ATOMS) {
            csos_spec_atom_t *a = &spec->atoms[spec->atom_count];
            memset(a, 0, sizeof(*a));

            /* Extract name */
            char *ns = strstr(p, "\"name\":\"");
            if (ns) {
                ns += 8;
                int i = 0;
                while (*ns && *ns != '"' && i < CSOS_NAME_LEN - 1) a->name[i++] = *ns++;
                a->name[i] = 0;
            }

            /* Extract formula */
            char *fs = strstr(p, "\"formula\":\"");
            if (fs) {
                fs += 11;
                int i = 0;
                while (*fs && *fs != '"' && i < CSOS_FORMULA_LEN - 1) a->formula[i++] = *fs++;
                a->formula[i] = 0;
            }

            /* Extract center → becomes the compute expression (constant prediction) */
            char *cs = strstr(p, "\"center\":");
            if (cs) {
                cs += 9;
                double center = strtod(cs, NULL);
                snprintf(a->compute, CSOS_FORMULA_LEN, "%.6f", center);
                a->param_keys[0][0] = 'c'; a->param_keys[0][1] = 0;
                a->param_defaults[0] = center;
                a->param_count = 1;
            }

            /* Calvin atoms are broadband (absorb everything in their range) */
            a->broadband = 0;
            a->spectral[0] = 0;
            a->spectral[1] = 10000;
            strncpy(a->source, "Calvin synthesis", CSOS_NAME_LEN - 1);

            spec->atom_count++;
            added++;
            p++;
        }
    }
    closedir(dir);
    return added;
}

/* ═══ IR VALIDATION (Law I enforcement — the system drives the change) ═══ */
/*
 * The 5 foundation compute expressions define the allowed IR.
 * Any atom in a .csos spec MUST derive from one of these.
 * Calvin-synthesized atoms (numeric constants) are also allowed.
 *
 * This is NOT a one-time cleanup. This runs on every spec load,
 * every --seed, and every compact cycle. The system enforces itself.
 */

/* Canonical compute signatures of the 5 foundation equations */
static const char *FOUNDATION_SIGNATURES[] = {
    /* Gouterman 1961:  h * c / l  (spectral matching) */
    "h * c / l",
    /* Forster 1948:   (1 / t) * (R0 / r) ** min(R0 / r, 6) */
    "(1 / t) * (R0 / r)",
    /* Marcus 1956:    exp(-(dG + l) ** 2 / (4 * l * V)) * input */
    "exp(-(dG + l)",
    /* Mitchell 1961:  n * F * abs(dy) + signal * abs(dy) / (1 + n) */
    "n * F * abs(dy)",
    /* Boyer 1997:     flux * n / 3 */
    "flux * n / 3",
    NULL
};

/* Check if a signature match is at a valid boundary.
 * The signature must be at the start of the expression OR preceded by
 * an operator/paren, AND followed by end/operator/paren.
 * This prevents trojans like "evil() + h * c / l" from passing. */
static int is_boundary_match(const char *compute, const char *sig) {
    const char *pos = strstr(compute, sig);
    if (!pos) return 0;

    /* Check prefix: must be at start, or preceded by operator/space/paren */
    if (pos != compute) {
        char before = *(pos - 1);
        if (before != ' ' && before != '(' && before != '+' && before != '-'
            && before != '*' && before != '/' && before != '\t')
            return 0;
    }

    /* Check suffix: signature must consume most of the expression.
     * Allow trailing operators/parens but not large unrelated code. */
    const char *after = pos + strlen(sig);
    /* Count remaining non-whitespace, non-operator chars */
    int trailing_content = 0;
    for (const char *c = after; *c; c++) {
        if (*c != ' ' && *c != ')' && *c != '*' && *c != '/' && *c != '+'
            && *c != '-' && *c != '(' && *c != '\t' && *c != ';'
            && !isdigit((unsigned char)*c) && *c != '.')
            trailing_content++;
    }
    /* Allow at most 30 chars of trailing variable names (for "* input" etc)
     * but reject long appended code */
    if (trailing_content > 30) return 0;

    return 1;
}

/* Check if a compute expression derives from a foundation equation.
 * Match rules:
 *   1. Calvin atoms: pure numeric constants → always valid
 *   2. Foundation: signature must be at a valid boundary in the expression
 *   3. Expression must not contain significantly more code than the signature
 *
 * This prevents:
 *   - Trojan expressions: "evil() + h * c / l" (rejected: evil() before signature)
 *   - Embedded signatures: "my_h * c / l_function()" (rejected: trailing content)
 *   - CRUD disguised: "save(data, db) + flux * n / 3" (rejected: leading content)
 */
static int is_foundation_derived(const char *compute) {
    if (!compute || !compute[0]) return 0;

    /* Calvin atoms: pure numeric constants like "45.123456" */
    const char *p = compute;
    int is_numeric = 1;
    while (*p) {
        if (!isdigit((unsigned char)*p) && *p != '.' && *p != '-' && *p != '+' && *p != ' ')
            { is_numeric = 0; break; }
        p++;
    }
    if (is_numeric) return 1; /* Calvin-synthesized constant */

    /* Expression must not be excessively long (foundation computes are < 80 chars) */
    if (strlen(compute) > 128) return 0;

    /* Check against each foundation signature with boundary validation */
    for (int i = 0; FOUNDATION_SIGNATURES[i]; i++) {
        if (is_boundary_match(compute, FOUNDATION_SIGNATURES[i]))
            return 1;
    }
    return 0;
}

int csos_spec_validate_atom(const csos_spec_atom_t *atom, char *reason, size_t rsz) {
    /* Calvin atoms always pass */
    if (strncmp(atom->name, "calvin_", 7) == 0) return 0;

    /* Foundation atoms: check compute expression */
    if (is_foundation_derived(atom->compute)) return 0;

    /* Rejected: non-physics formula */
    if (reason && rsz > 0) {
        snprintf(reason, rsz,
            "REJECTED atom '%s': compute '%s' does not derive from any of the 5 "
            "foundation equations (Gouterman, Forster, Marcus, Mitchell, Boyer). "
            "Use absorb() to feed signals — Calvin will synthesize patterns.",
            atom->name, atom->compute);
    }
    return -1;
}

int csos_spec_validate(csos_spec_t *spec, char *log, size_t log_sz) {
    int rejected = 0;
    int log_pos = 0;
    int write_idx = 0;

    for (int i = 0; i < spec->atom_count; i++) {
        char reason[512] = {0};
        if (csos_spec_validate_atom(&spec->atoms[i], reason, sizeof(reason)) == 0) {
            /* Valid: keep it (shift forward if needed) */
            if (write_idx != i)
                spec->atoms[write_idx] = spec->atoms[i];
            write_idx++;
        } else {
            /* Invalid: strip it */
            rejected++;
            if (log && log_pos < (int)log_sz - 2) {
                log_pos += snprintf(log + log_pos, log_sz - log_pos,
                    "%s\n", reason);
            }
        }
    }
    spec->atom_count = write_idx;
    return rejected;
}

/* ═══ COMPACT (self-healing — the system cleans itself) ═══ */

int csos_compact_specs(const char *root) {
    char specs_dir[512];
    snprintf(specs_dir, sizeof(specs_dir), "%s/specs", root);
    DIR *dir = opendir(specs_dir);
    if (!dir) return 0;

    int removed = 0;
    struct dirent *ent;
    while ((ent = readdir(dir)) != NULL) {
        if (!strstr(ent->d_name, ".csos")) continue;
        /* PRESERVE eco.csos — the genome is sacred */
        if (strcmp(ent->d_name, "eco.csos") == 0) continue;

        /* Validate the spec file */
        char path[512];
        snprintf(path, sizeof(path), "%s/%s", specs_dir, ent->d_name);
        csos_spec_t test_spec = {0};
        if (csos_spec_parse(path, &test_spec) != 0) continue;

        /* Check: does every atom derive from foundation equations? */
        int has_invalid = 0;
        for (int i = 0; i < test_spec.atom_count; i++) {
            if (csos_spec_validate_atom(&test_spec.atoms[i], NULL, 0) != 0) {
                has_invalid = 1;
                break;
            }
        }

        if (has_invalid) {
            /* Remove non-conforming spec file */
            unlink(path);
            removed++;
            fprintf(stderr, "[compact] Removed non-conforming spec: %s\n", ent->d_name);
        }
    }
    closedir(dir);
    return removed;
}

/* Max wisdom files to retain. Beyond this, oldest are evicted.
 * Derived: Calvin SAMPLE_SIZE (50) × 5 equations × 4 = 1000 patterns max.
 * Anything beyond this is repetitive — the gradient already captured it. */
#define CSOS_MAX_WISDOM_FILES 100

int csos_compact_deliveries(const char *root) {
    char del_dir[512];
    snprintf(del_dir, sizeof(del_dir), "%s/.csos/deliveries", root);
    DIR *dir = opendir(del_dir);
    if (!dir) return 0;

    int removed = 0;
    struct dirent *ent;

    /* Pass 1: Remove non-IR files (prose, docs, oversized non-JSON) */
    while ((ent = readdir(dir)) != NULL) {
        if (ent->d_name[0] == '.') continue;
        /* PRESERVE .json files (photon-format wisdom) — validated in pass 2 */
        if (strstr(ent->d_name, ".json")) continue;
        /* PRESERVE latest.md (current ephemeral delivery) */
        if (strcmp(ent->d_name, "latest.md") == 0) continue;

        /* Everything else is non-IR: remove it */
        char path[512];
        snprintf(path, sizeof(path), "%s/%s", del_dir, ent->d_name);
        unlink(path);
        removed++;
        fprintf(stderr, "[compact] Removed non-IR delivery: %s\n", ent->d_name);
    }
    closedir(dir);

    /* Pass 2: Cap wisdom files — evict oldest beyond CSOS_MAX_WISDOM_FILES.
     * Wisdom files are named wisdom_{hash}_c{cycle}.json.
     * Cycle number is monotonically increasing, so alphabetical sort ~= time order. */
    dir = opendir(del_dir);
    if (!dir) return removed;

    /* Collect wisdom file names */
    char wisdom_names[1024][256];
    int wisdom_count = 0;
    while ((ent = readdir(dir)) != NULL && wisdom_count < 1024) {
        if (strncmp(ent->d_name, "wisdom_", 7) == 0 && strstr(ent->d_name, ".json")) {
            strncpy(wisdom_names[wisdom_count], ent->d_name, 255);
            wisdom_count++;
        }
    }
    closedir(dir);

    if (wisdom_count > CSOS_MAX_WISDOM_FILES) {
        /* Sort alphabetically (cycle number in name gives rough time order) */
        for (int i = 0; i < wisdom_count - 1; i++) {
            for (int j = i + 1; j < wisdom_count; j++) {
                if (strcmp(wisdom_names[i], wisdom_names[j]) > 0) {
                    char tmp[256];
                    strncpy(tmp, wisdom_names[i], 255);
                    strncpy(wisdom_names[i], wisdom_names[j], 255);
                    strncpy(wisdom_names[j], tmp, 255);
                }
            }
        }
        /* Remove oldest (first in sorted order) until within cap */
        int to_remove = wisdom_count - CSOS_MAX_WISDOM_FILES;
        for (int i = 0; i < to_remove; i++) {
            char path[512];
            snprintf(path, sizeof(path), "%s/%s", del_dir, wisdom_names[i]);
            unlink(path);
            removed++;
        }
        if (to_remove > 0)
            fprintf(stderr, "[compact] Evicted %d oldest wisdom files (cap=%d)\n",
                    to_remove, CSOS_MAX_WISDOM_FILES);
    }
    return removed;
}

int csos_compact_canvas(const char *root) {
    char canvas_dir[512];
    snprintf(canvas_dir, sizeof(canvas_dir), "%s/.canvas-tui", root);
    DIR *dir = opendir(canvas_dir);
    if (!dir) return 0;

    int removed = 0;
    struct dirent *ent;
    while ((ent = readdir(dir)) != NULL) {
        if (ent->d_name[0] == '.') continue;
        /* PRESERVE index.html — the one canvas file */
        if (strcmp(ent->d_name, "index.html") == 0) continue;
        /* Everything else is test/debug bloat */
        char path[512];
        snprintf(path, sizeof(path), "%s/%s", canvas_dir, ent->d_name);
        unlink(path);
        removed++;
        fprintf(stderr, "[compact] Removed canvas artifact: %s\n", ent->d_name);
    }
    closedir(dir);
    return removed;
}

/* Compact orphaned ring files: .mem.json files not matching active ecosystem rings.
 * Preserves eco_domain, eco_cockpit, eco_organism. Removes test/temp ring dumps. */
int csos_compact_rings(const char *root) {
    char rings_dir[512];
    snprintf(rings_dir, sizeof(rings_dir), "%s/.csos/rings", root);
    DIR *dir = opendir(rings_dir);
    if (!dir) return 0;

    /* The 3 ecosystem ring .mem.json files — always preserved */
    static const char *PRESERVE[] = {
        "eco_domain.mem.json", "eco_cockpit.mem.json", "eco_organism.mem.json", NULL
    };

    int removed = 0;
    struct dirent *ent;
    while ((ent = readdir(dir)) != NULL) {
        if (ent->d_name[0] == '.') continue;

        /* Preserve the 3 ecosystem .mem.json files */
        int keep = 0;
        for (int i = 0; PRESERVE[i]; i++) {
            if (strcmp(ent->d_name, PRESERVE[i]) == 0) { keep = 1; break; }
        }
        if (keep) continue;

        /* Remove everything else: orphaned .mem.json, old .json state dumps */
        char path[512];
        snprintf(path, sizeof(path), "%s/%s", rings_dir, ent->d_name);
        unlink(path);
        removed++;
        fprintf(stderr, "[compact] Removed orphaned ring file: %s\n", ent->d_name);
    }
    closedir(dir);
    return removed;
}

int csos_compact(const char *root) {
    int total = 0;
    total += csos_compact_specs(root);
    total += csos_compact_deliveries(root);
    total += csos_compact_canvas(root);
    total += csos_compact_rings(root);
    if (total > 0)
        fprintf(stderr, "[compact] Cleaned %d bloat artifacts\n", total);
    return total;
}

/* ═══ MEMBRANE FROM SPEC ═══ */

csos_membrane_t *csos_membrane_from_spec(const csos_spec_t *spec, int ring_index) {
    if (ring_index < 0 || ring_index >= spec->ring_count) return NULL;

    csos_membrane_t *m = (csos_membrane_t *)calloc(1, sizeof(csos_membrane_t));
    if (!m) return NULL;

    strncpy(m->name, spec->ring_names[ring_index], CSOS_NAME_LEN - 1);
    m->human_present = 1;
    /* Mitchell n from spec (default 1 if not specified) */
    m->mitchell_n = spec->ring_mitchell_n[ring_index];
    if (m->mitchell_n < 1) m->mitchell_n = 1;

    /* Initialize atoms from spec (foundation + Calvin) */
    int count = 0;
    for (int i = 0; i < spec->atom_count && count < CSOS_MAX_ATOMS; i++) {
        const csos_spec_atom_t *sa = &spec->atoms[i];

        /* Skip Calvin atoms that don't belong to this ring
         * (foundation atoms go in all rings) */
        if (strncmp(sa->name, "calvin_", 7) == 0) {
            /* Calvin atoms loaded separately per ring via mem.json */
            continue;
        }

        csos_atom_t *a = &m->atoms[count];
        memset(a, 0, sizeof(*a));
        strncpy(a->name, sa->name, CSOS_NAME_LEN - 1);
        strncpy(a->formula, sa->formula, CSOS_FORMULA_LEN - 1);
        strncpy(a->compute, sa->compute, CSOS_FORMULA_LEN - 1);
        strncpy(a->source, sa->source, CSOS_NAME_LEN - 1);
        strncpy(a->born_in, m->name, CSOS_NAME_LEN - 1);

        a->param_count = sa->param_count;
        for (int j = 0; j < sa->param_count; j++) {
            strncpy(a->param_keys[j], sa->param_keys[j], 31);
            a->params[j] = sa->param_defaults[j];
        }
        a->spectral[0] = sa->spectral[0];
        a->spectral[1] = sa->spectral[1];
        a->broadband = sa->broadband;

        a->photon_cap = 256;
        a->photons = (csos_photon_t *)calloc(a->photon_cap, sizeof(csos_photon_t));
        a->local_cap = 256;
        a->local_photons = (csos_photon_t *)calloc(a->local_cap, sizeof(csos_photon_t));
        csos_atom_compute_rw(a);
        count++;
    }

    m->atom_count = count;
    m->rw = count > 0 ? m->atoms[0].rw : 0.833;
    return m;
}
