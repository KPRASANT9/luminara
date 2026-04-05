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

        /* Inside ring block */
        if (in_ring && strchr(line, '}') && brace_depth <= 0) {
            in_ring = 0;
            brace_depth = 0;
        }
    }

    fclose(f);
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

/* ═══ MEMBRANE FROM SPEC ═══ */

csos_membrane_t *csos_membrane_from_spec(const csos_spec_t *spec, int ring_index) {
    if (ring_index < 0 || ring_index >= spec->ring_count) return NULL;

    csos_membrane_t *m = (csos_membrane_t *)calloc(1, sizeof(csos_membrane_t));
    if (!m) return NULL;

    strncpy(m->name, spec->ring_names[ring_index], CSOS_NAME_LEN - 1);
    m->human_present = 1;

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
