#!/bin/bash
# CSOS Health Watchdog — uses native binary only
# Reads organism ring health → auto-corrects → alerts if critical
#
# Features:
#   - Health check via diagnose
#   - Auto-save + snapshot on every run
#   - Alert on degraded state via configured notify channel
#   - IST timezone awareness (tracks active ring)
#
# Install: crontab -e → */15 * * * * /path/to/watchdog.sh
#
# Environment (optional):
#   CSOS_NOTIFY_URL     — Webhook URL for alerts (Slack, Discord, generic)
#   CSOS_NOTIFY_CHANNEL — "webhook", "slack", or "file" (default: file)

set -euo pipefail
cd "$(dirname "$0")/.."

LOG=".csos/watchdog.log"
SNAPSHOT=".csos/snapshot.json"
ALERTS=".csos/alerts.jsonl"
mkdir -p .csos

log() { echo "$(date -Iseconds) WATCHDOG $1" >> "$LOG"; }

# Ensure binary exists
if [ ! -f "./csos" ]; then
    log "ERROR: ./csos binary not found"
    exit 1
fi

# Check if HTTP daemon is running
DAEMON_UP=0
if curl -sf http://localhost:4200/api/state >/dev/null 2>&1; then
    DAEMON_UP=1
fi

log "START daemon=$DAEMON_UP"

if [ "$DAEMON_UP" -eq 1 ]; then
    # Use HTTP API (daemon mode — full state available)

    # Run diagnose
    DIAG=$(curl -sf -X POST http://localhost:4200/api/command \
        -H 'Content-Type: application/json' \
        -d '{"action":"diagnose"}' 2>/dev/null || echo '{"status":"unreachable"}')
    STATUS=$(echo "$DIAG" | grep -o '"status":"[^"]*"' | cut -d'"' -f4 || echo "unknown")

    # Get organism decision
    ORG=$(curl -sf -X POST http://localhost:4200/api/command \
        -H 'Content-Type: application/json' \
        -d '{"action":"see","ring":"eco_organism","detail":"cockpit"}' 2>/dev/null || echo '{}')
    DECISION=$(echo "$ORG" | grep -o '"decision":"[^"]*"' | cut -d'"' -f4 || echo "unknown")

    log "STATUS=$STATUS DECISION=$DECISION"

    case "$STATUS" in
        healthy|pass)
            log "HEALTHY"
            ;;
        degraded|*)
            log "DEGRADED — running stabilization + alerting"
            # Feed a health-check signal
            curl -sf -X POST http://localhost:4200/api/command \
                -H 'Content-Type: application/json' \
                -d '{"action":"absorb","substrate":"watchdog","output":"health:degraded status:alert"}' \
                >/dev/null 2>&1
            # Alert via configured channel
            if [ -n "${CSOS_NOTIFY_URL:-}" ]; then
                ALERT_MSG="CSOS DEGRADED at $(date -Iseconds) | status=$STATUS decision=$DECISION"
                if [ "${CSOS_NOTIFY_CHANNEL:-webhook}" = "slack" ]; then
                    curl -sf -X POST "$CSOS_NOTIFY_URL" \
                        -H 'Content-Type: application/json' \
                        -d "{\"text\":\"$ALERT_MSG\"}" \
                        >/dev/null 2>&1
                else
                    curl -sf -X POST "$CSOS_NOTIFY_URL" \
                        -H 'Content-Type: application/json' \
                        -d "{\"event\":\"DEGRADED\",\"message\":\"$ALERT_MSG\"}" \
                        >/dev/null 2>&1
                fi
            fi
            # Also log to alerts file
            echo "{\"event\":\"DEGRADED\",\"time\":\"$(date -Iseconds)\",\"status\":\"$STATUS\",\"decision\":\"$DECISION\"}" >> "$ALERTS"
            ;;
    esac

    # Save state + snapshot
    curl -sf -X POST http://localhost:4200/api/command \
        -H 'Content-Type: application/json' \
        -d '{"action":"save"}' >/dev/null 2>&1

else
    # CLI mode (daemon not running — use binary directly)
    DIAG=$(echo '{"action":"diagnose"}' | ./csos 2>/dev/null | tail -1)
    STATUS=$(echo "$DIAG" | grep -o '"status":"[^"]*"' | cut -d'"' -f4 || echo "unknown")

    log "STATUS=$STATUS (cli mode)"

    case "$STATUS" in
        healthy|pass)
            log "HEALTHY"
            ;;
        degraded|*)
            log "DEGRADED — running stabilization"
            echo '{"action":"absorb","substrate":"watchdog","output":"health check"}' | ./csos 2>/dev/null >> "$LOG"
            echo "{\"event\":\"DEGRADED\",\"time\":\"$(date -Iseconds)\",\"status\":\"$STATUS\"}" >> "$ALERTS"
            ;;
    esac

    # Save state
    echo '{"action":"save"}' | ./csos 2>/dev/null >> "$LOG"
fi

# Determine active ring based on IST hour
HOUR=$(TZ='Asia/Kolkata' date +%H | sed 's/^0//')
if [ "$HOUR" -ge 5 ] && [ "$HOUR" -lt 9 ]; then
    ACTIVE_RING="asia"
elif [ "$HOUR" -ge 9 ] && [ "$HOUR" -lt 16 ]; then
    ACTIVE_RING="india"
elif [ "$HOUR" -ge 13 ] && [ "$HOUR" -lt 18 ]; then
    ACTIVE_RING="europe"
elif [ "$HOUR" -ge 19 ] || [ "$HOUR" -lt 2 ]; then
    ACTIVE_RING="us"
else
    ACTIVE_RING="dark"
fi
log "ACTIVE_RING=$ACTIVE_RING"

log "END"
