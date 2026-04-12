/*
 * CSOS Store -- Persistence + auto-save + dual-reactor state.
 *
 * Provides:
 *   1. Auto-save after every N absorptions (configurable)
 *   2. Dual-reactor state persistence (ATP/NADPH pools)
 *   3. Webhook auto-notify on Boyer EXECUTE decisions
 *   4. Organism snapshot for cron/watchdog integration
 *
 * All state persisted to .csos/rings/ as JSON.
 * Seed bank persisted separately to .csos/sessions/.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

/* ═══ AUTO-SAVE CONFIGURATION ═══
 * Auto-save fires every CSOS_AUTOSAVE_INTERVAL absorptions.
 * Default: every 100 absorptions. Override with env CSOS_AUTOSAVE_INTERVAL.
 * Set to 0 to disable auto-save (only manual save + shutdown save).
 */
#define CSOS_AUTOSAVE_DEFAULT  100

/* ═══ AUTO-NOTIFY CONFIGURATION ═══
 * When Boyer EXECUTE fires and a webhook URL is configured,
 * auto-notify pushes the decision to the configured endpoint.
 * Set env CSOS_NOTIFY_URL to enable.
 * Set env CSOS_NOTIFY_CHANNEL to "webhook", "slack", or "file" (default: webhook).
 */

/* Forward declarations (defined in membrane.c, included after store.c in unity build) */
int csos_organism_save(csos_organism_t *org);

static int _autosave_interval = 0;
static int _autosave_init = 0;
static uint32_t _total_absorptions = 0;

/* Notify config (loaded from env on first use) */
static char _notify_url[1024] = {0};
static char _notify_channel[64] = {0};
static int _notify_init = 0;

/* ═══ INIT ═══ */

static void store_init_autosave(void) {
    if (_autosave_init) return;
    _autosave_init = 1;
    const char *env = getenv("CSOS_AUTOSAVE_INTERVAL");
    if (env) {
        _autosave_interval = atoi(env);
    } else {
        _autosave_interval = CSOS_AUTOSAVE_DEFAULT;
    }
}

static void store_init_notify(void) {
    if (_notify_init) return;
    _notify_init = 1;
    const char *url = getenv("CSOS_NOTIFY_URL");
    if (url && url[0]) {
        strncpy(_notify_url, url, sizeof(_notify_url) - 1);
    }
    const char *ch = getenv("CSOS_NOTIFY_CHANNEL");
    if (ch && ch[0]) {
        strncpy(_notify_channel, ch, sizeof(_notify_channel) - 1);
    } else {
        strncpy(_notify_channel, "webhook", sizeof(_notify_channel) - 1);
    }
}

/* ═══ AUTO-SAVE HOOK ═══
 * Called after every membrane_absorb() in the absorb handler.
 * Fires save when absorption count hits interval.
 */
static int store_maybe_autosave(csos_organism_t *org) {
    store_init_autosave();
    if (_autosave_interval <= 0) return 0;
    _total_absorptions++;
    if ((_total_absorptions % (uint32_t)_autosave_interval) == 0) {
        return csos_organism_save(org);
    }
    return 0;
}

/* ═══ AUTO-NOTIFY ON EXECUTE ═══
 * Called after absorb when Boyer decision is EXECUTE.
 * Sends decision payload to configured webhook/slack/file.
 * Non-blocking: forks a child to do the HTTP POST.
 */
static void store_notify_execute(const char *substrate, double gradient,
                                  double speed, double rw, double F,
                                  double motor_strength) {
    store_init_notify();
    if (!_notify_url[0]) return;  /* No URL configured, skip */

    /* Build payload */
    char payload[2048];
    time_t now = time(NULL);
    struct tm *tm = localtime(&now);
    char timestr[64];
    strftime(timestr, sizeof(timestr), "%Y-%m-%d %H:%M:%S IST", tm);

    snprintf(payload, sizeof(payload),
        "{\"event\":\"EXECUTE\","
        "\"substrate\":\"%s\","
        "\"time\":\"%s\","
        "\"gradient\":%.0f,"
        "\"speed\":%.4f,"
        "\"rw\":%.4f,"
        "\"F\":%.4f,"
        "\"conviction\":%.2f,"
        "\"motor_strength\":%.3f}",
        substrate, timestr, gradient, speed, rw, F,
        (1.0 - F) * 100.0, motor_strength);

    if (strcmp(_notify_channel, "file") == 0) {
        /* Append to alert log file */
        mkdir(".csos", 0755);
        FILE *f = fopen(".csos/alerts.jsonl", "a");
        if (f) {
            fprintf(f, "%s\n", payload);
            fclose(f);
        }
        return;
    }

    /* Fork to avoid blocking the membrane */
    pid_t pid = fork();
    if (pid == 0) {
        /* Child: fire and forget */
        char cmd[4096];
        if (strcmp(_notify_channel, "slack") == 0) {
            /* Slack formatting */
            char text[1024];
            snprintf(text, sizeof(text),
                "EXECUTE %s | grad=%.0f speed=%.4f F=%.4f conviction=%.0f%% | %s",
                substrate, gradient, speed, F, (1.0 - F) * 100.0, timestr);
            snprintf(cmd, sizeof(cmd),
                "curl -sf -X POST '%s' "
                "-H 'Content-Type: application/json' "
                "-d '{\"text\":\"%s\"}' "
                ">/dev/null 2>&1", _notify_url, text);
        } else {
            /* Generic webhook */
            snprintf(cmd, sizeof(cmd),
                "curl -sf -X POST '%s' "
                "-H 'Content-Type: application/json' "
                "-d '%s' "
                ">/dev/null 2>&1", _notify_url, payload);
        }
        execl("/bin/sh", "sh", "-c", cmd, (char *)NULL);
        _exit(1);
    }
    /* Parent: don't wait (fire and forget) */
}

/* ═══ SNAPSHOT: Quick organism state dump ═══
 * Used by watchdog and cron scripts for quick health checks.
 * Writes a compact JSON to .csos/snapshot.json.
 */
static int store_write_snapshot(const csos_organism_t *org) {
    mkdir(".csos", 0755);
    FILE *f = fopen(".csos/snapshot.json", "w");
    if (!f) return -1;

    time_t now = time(NULL);
    struct tm *tm = localtime(&now);
    char timestr[64];
    strftime(timestr, sizeof(timestr), "%Y-%m-%dT%H:%M:%S", tm);

    fprintf(f, "{\"timestamp\":\"%s\",\"rings\":%d,\"absorptions\":%u,\"membranes\":[",
            timestr, org->count, _total_absorptions);

    for (int i = 0; i < org->count; i++) {
        const csos_membrane_t *m = org->membranes[i];
        if (!m) continue;
        if (i > 0) fprintf(f, ",");
        const char *dec[] = {"EXPLORE","EXECUTE","ASK","STORE"};
        fprintf(f,
            "{\"name\":\"%s\",\"gradient\":%.0f,\"speed\":%.4f,"
            "\"F\":%.4f,\"rw\":%.3f,\"decision\":\"%s\","
            "\"cycles\":%u,\"atoms\":%d,\"motor_count\":%d,"
            "\"atp\":%.1f,\"nadph\":%.1f,\"reactor_balance\":%.2f}",
            m->name, m->gradient, m->speed,
            m->F, m->rw, dec[m->decision & 3],
            m->cycles, m->atom_count, m->motor_count,
            m->atp_pool, m->nadph_pool, m->reactor_balance);
    }
    fprintf(f, "]}\n");
    fclose(f);
    return 0;
}
