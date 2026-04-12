#!/usr/bin/env bash
# CSOS launcher — one entrypoint, docker under the hood.
#
#   ./scripts/csos-startup.sh              # start daemon (default: up)
#   ./scripts/csos-startup.sh up           # start csos daemon in background
#   ./scripts/csos-startup.sh down         # stop daemon (keep volumes)
#   ./scripts/csos-startup.sh restart      # down + up
#   ./scripts/csos-startup.sh shell        # open interactive opencode session
#   ./scripts/csos-startup.sh exec         # attach bash into running csos daemon
#   ./scripts/csos-startup.sh term         # fresh shell in dev image w/ repo mounted
#   ./scripts/csos-startup.sh status       # health + container state
#   ./scripts/csos-startup.sh logs         # tail daemon logs
#   ./scripts/csos-startup.sh rebuild      # rebuild images (csos + opencode)
#   ./scripts/csos-startup.sh clean        # down + remove volumes (destructive)

set -euo pipefail

DIR="$(cd "$(dirname "$0")/.." && pwd)"
cd "$DIR"

PORT="${CSOS_PORT:-4200}"
COMPOSE=(docker compose)

# ---- preflight ---------------------------------------------------------------
preflight() {
  command -v docker >/dev/null 2>&1 || { echo "docker not installed"; exit 1; }
  docker info >/dev/null 2>&1      || { echo "docker daemon not running"; exit 1; }

  if [ ! -f .env ]; then
    if [ -f .env.example ]; then
      echo ".env missing — copy .env.example and fill in secrets:"
      echo "  cp .env.example .env && \$EDITOR .env"
    else
      echo ".env missing"
    fi
    exit 1
  fi

  mkdir -p csos-data
}

wait_healthy() {
  local tries=30
  while [ $tries -gt 0 ]; do
    if curl -sf -m 1 "http://localhost:$PORT/api/state" >/dev/null 2>&1; then
      return 0
    fi
    sleep 1; tries=$((tries-1))
  done
  return 1
}

# ---- commands ----------------------------------------------------------------
cmd_up() {
  preflight
  if curl -sf -m 1 "http://localhost:$PORT/api/state" >/dev/null 2>&1; then
    echo "CSOS already running on :$PORT"; return 0
  fi
  "${COMPOSE[@]}" up -d csos
  if wait_healthy; then
    cat <<EOF
CSOS daemon up.

  Canvas:  http://localhost:$PORT
  SSE:     http://localhost:$PORT/events
  API:     http://localhost:$PORT/api/command (POST)
  State:   http://localhost:$PORT/api/state   (GET)

Next:
  ./scripts/csos-startup.sh shell     # opencode session
  ./scripts/csos-startup.sh logs      # tail daemon
EOF
  else
    echo "CSOS failed to become healthy — check: ./scripts/csos-startup.sh logs"
    exit 1
  fi
}

cmd_down()    { "${COMPOSE[@]}" down; }
cmd_restart() { cmd_down; cmd_up; }
cmd_logs()    { "${COMPOSE[@]}" logs -f csos; }

cmd_status() {
  "${COMPOSE[@]}" ps
  echo
  if curl -sf -m 1 "http://localhost:$PORT/api/state" >/dev/null 2>&1; then
    echo "health: OK (:$PORT)"
  else
    echo "health: DOWN (:$PORT)"
  fi
}

cmd_rebuild() {
  preflight
  "${COMPOSE[@]}" build csos
  "${COMPOSE[@]}" --profile dev build opencode
}

cmd_clean() {
  read -rp "Remove volumes (opencode sessions, uv cache, csos-data)? [y/N] " ans
  [[ "$ans" == "y" || "$ans" == "Y" ]] || return 0
  "${COMPOSE[@]}" down -v
  rm -rf csos-data
}

# Interactive opencode TUI — dev profile, repo bind-mounted, daemon reachable.
cmd_shell() {
  preflight
  curl -sf -m 1 "http://localhost:$PORT/api/state" >/dev/null 2>&1 || cmd_up
  "${COMPOSE[@]}" --profile dev run --rm opencode
}

# Attach bash into the already-running csos daemon container.
# Useful for poking at live state in .csos/ or running one-off csos commands.
cmd_exec() {
  if ! docker ps --format '{{.Names}}' | grep -q '^csos$'; then
    echo "csos container not running — start it with: $0 up"
    exit 1
  fi
  docker exec -it csos /bin/bash 2>/dev/null || docker exec -it csos /bin/sh
}

# Fresh ephemeral shell in the dev image with the repo volume-mounted.
# Same mounts as the opencode service, but drops you at a bash prompt instead
# of launching the TUI — good for uvx/curl/debugging without touching the host.
cmd_term() {
  preflight
  "${COMPOSE[@]}" --profile dev run --rm --entrypoint /bin/bash opencode
}

case "${1:-up}" in
  up|start)       cmd_up ;;
  down|stop)      cmd_down ;;
  restart)        cmd_restart ;;
  shell|dev)      cmd_shell ;;
  exec|attach)    cmd_exec ;;
  term|bash)      cmd_term ;;
  logs|tail)      cmd_logs ;;
  status|ps)      cmd_status ;;
  rebuild|build)  cmd_rebuild ;;
  clean|nuke)     cmd_clean ;;
  -h|--help|help)
    sed -n '2,16p' "$0" | sed 's/^# \{0,1\}//'
    ;;
  *)
    echo "usage: $0 {up|down|restart|shell|exec|term|logs|status|rebuild|clean}"
    exit 2
    ;;
esac
