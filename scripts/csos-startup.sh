#!/bin/bash
# CSOS Startup — starts daemon in HTTP mode (SSE on port 4200)
# Run this on OpenCode start, or manually: ./scripts/csos-startup.sh
#
# Boot sequence:
#   1. Daemon starts in HTTP mode + stdin/stdout pipe mode
#   2. TUI opens (OpenCode's SolidJS terminal)
#   3. Human types "open canvas" OR agent calls: csos-canvas action=open
#   4. Browser opens http://localhost:4200
#   5. Canvas connects via EventSource to /events
#   6. Both TUI and canvas show the same state, live

PORT=${CSOS_SSE_PORT:-4200}
DIR="$(cd "$(dirname "$0")/.." && pwd)"
cd "$DIR"

# Check if daemon SSE is already running
if curl -s -m 1 "http://localhost:$PORT/api/state" > /dev/null 2>&1; then
    echo "CSOS daemon already running on port $PORT"
    exit 0
fi

# Start daemon with SSE in background
CSOS_SSE=1 CSOS_SSE_PORT=$PORT python3 scripts/csos-daemon.py --sse &
DAEMON_PID=$!

# Wait for boot
sleep 2

if curl -s -m 1 "http://localhost:$PORT/api/state" > /dev/null 2>&1; then
    echo "CSOS daemon started (pid=$DAEMON_PID, port=$PORT)"
    echo "Canvas: http://localhost:$PORT"
    echo "SSE:    http://localhost:$PORT/events"
    echo "API:    http://localhost:$PORT/api/state"
else
    echo "CSOS daemon failed to start"
    exit 1
fi
