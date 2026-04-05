#!/bin/bash
# CSOS Startup — native binary daemon (single process, zero Python)
#
# Architecture (Replit-style):
#   ./csos --http PORT    = the entire daemon (physics + HTTP + SSE + canvas)
#   .canvas-tui/index.html = served by the binary (GET /)
#   specs/*.csos           = loaded at runtime (Law I: spec IS code)
#   .csos/                 = persistent state (rings, sessions, deliveries)

PORT=${CSOS_PORT:-4200}
DIR="$(cd "$(dirname "$0")/.." && pwd)"
cd "$DIR"

# Build if binary doesn't exist
if [ ! -f ./csos ]; then
    echo "Building native binary..."
    make || { echo "Build failed"; exit 1; }
fi

# Check if already running
if curl -s -m 1 "http://localhost:$PORT/api/state" > /dev/null 2>&1; then
    echo "CSOS already running on port $PORT"
    exit 0
fi

# Start native daemon
./csos --http $PORT &
DAEMON_PID=$!
sleep 1

if curl -s -m 1 "http://localhost:$PORT/api/state" > /dev/null 2>&1; then
    echo "CSOS native daemon started (pid=$DAEMON_PID)"
    echo ""
    echo "  Canvas:  http://localhost:$PORT"
    echo "  SSE:     http://localhost:$PORT/events"
    echo "  API:     http://localhost:$PORT/api/command (POST)"
    echo "  State:   http://localhost:$PORT/api/state (GET)"
    echo ""
    echo "  Single binary. Zero Python. Spec-driven. LLVM JIT."
else
    echo "CSOS daemon failed to start"
    exit 1
fi
