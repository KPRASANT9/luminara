#!/bin/bash
# CSOS Health Watchdog — uses native binary only
# Reads organism ring health → auto-corrects → alerts if critical
#
# Install: crontab -e → */15 * * * * /path/to/watchdog.sh

set -euo pipefail
cd "$(dirname "$0")/.."

LOG=".csos/watchdog.log"
mkdir -p .csos

echo "$(date -Iseconds) WATCHDOG START" >> "$LOG"

# Ensure binary exists
if [ ! -f "./csos" ]; then
    echo "$(date -Iseconds) ERROR: ./csos binary not found" >> "$LOG"
    exit 1
fi

# Run diagnose via native binary
DIAG=$(echo '{"action":"diagnose"}' | ./csos 2>/dev/null | tail -1)
STATUS=$(echo "$DIAG" | grep -o '"status":"[^"]*"' | cut -d'"' -f4)
ISSUES=$(echo "$DIAG" | grep -o '"issues":\[[^]]*\]' | tr ',' '\n' | wc -l)

echo "$(date -Iseconds) STATUS=$STATUS ISSUES=$ISSUES" >> "$LOG"

case "$STATUS" in
  healthy|pass)
    echo "$(date -Iseconds) HEALTHY" >> "$LOG"
    ;;
  degraded|*)
    echo "$(date -Iseconds) DEGRADED — running stabilization" >> "$LOG"
    # Feed a health-check signal through the membrane
    echo '{"action":"absorb","substrate":"watchdog","output":"health check"}' | ./csos 2>/dev/null >> "$LOG"
    ;;
esac

# Save state
echo '{"action":"save"}' | ./csos 2>/dev/null >> "$LOG"

echo "$(date -Iseconds) WATCHDOG END" >> "$LOG"
