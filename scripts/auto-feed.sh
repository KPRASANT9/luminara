#!/bin/bash
# CSOS Auto-Feed — IST-cadence autonomous data ingestion
#
# Fetches market data from MCP servers via the HTTP API and feeds
# it into the membrane. No agent needed. Pure data ingestion.
#
# Usage:
#   ./scripts/auto-feed.sh <phase>
#
# Phases (aligned to IST market hours):
#   asia_open     05:30 IST — Tokyo/Sydney/Seoul open
#   india_open    09:15 IST — NSE opens, Asia mid-session
#   europe_open   13:30 IST — London/Frankfurt open
#   india_close   15:30 IST — NSE closes, predict US
#   us_open       19:00 IST — NYSE opens (London-NYSE overlap)
#   golden_hour   20:30 IST — All rings processed, max information
#   us_close      01:30 IST — NYSE closes, bridges through dark window
#
# Install as cron (IST = UTC+5:30, so convert):
#   0  0  * * 1-5  /path/to/scripts/auto-feed.sh asia_open       # 05:30 IST = 00:00 UTC
#   45 3  * * 1-5  /path/to/scripts/auto-feed.sh india_open      # 09:15 IST = 03:45 UTC
#   0  8  * * 1-5  /path/to/scripts/auto-feed.sh europe_open     # 13:30 IST = 08:00 UTC
#   0  10 * * 1-5  /path/to/scripts/auto-feed.sh india_close     # 15:30 IST = 10:00 UTC
#   30 13 * * 1-5  /path/to/scripts/auto-feed.sh us_open         # 19:00 IST = 13:30 UTC
#   0  15 * * 1-5  /path/to/scripts/auto-feed.sh golden_hour     # 20:30 IST = 15:00 UTC
#   0  20 * * 2-6  /path/to/scripts/auto-feed.sh us_close        # 01:30 IST = 20:00 UTC (prev day)
#
# Requirements:
#   - csos HTTP daemon running on localhost:4200
#   - .env file with API keys sourced

set -euo pipefail
cd "$(dirname "$0")/.."

PHASE="${1:-}"
API="http://localhost:4200/api/command"
LOG=".csos/auto-feed.log"
mkdir -p .csos

log() { echo "$(date -Iseconds) [$PHASE] $1" >> "$LOG"; }

post() {
    local payload="$1"
    curl -sf -X POST "$API" \
        -H 'Content-Type: application/json' \
        -d "$payload" 2>/dev/null || echo '{"error":"daemon unreachable"}'
}

# Check daemon is alive
if ! curl -sf "$API" -d '{"action":"ping"}' >/dev/null 2>&1; then
    log "ERROR: CSOS daemon not running on port 4200"
    exit 1
fi

log "START"

case "$PHASE" in

  asia_open)
    # 05:30 IST — Feed Asia indexes + overnight bridges
    log "Feeding Asia open + overnight bridges"
    post '{"action":"batch","items":"[{\"substrate\":\"nikkei\",\"output\":\"phase:asia_open\"},{\"substrate\":\"kospi\",\"output\":\"phase:asia_open\"},{\"substrate\":\"hangseng\",\"output\":\"phase:asia_open\"},{\"substrate\":\"asx200\",\"output\":\"phase:asia_open\"},{\"substrate\":\"shanghai\",\"output\":\"phase:asia_open\"}]"}'
    # Bridges that traded overnight
    post '{"action":"batch","items":"[{\"substrate\":\"es_futures\",\"output\":\"phase:overnight\"},{\"substrate\":\"bitcoin\",\"output\":\"phase:overnight\"},{\"substrate\":\"gold\",\"output\":\"phase:overnight\"}]"}'
    # Currencies
    post '{"action":"batch","items":"[{\"substrate\":\"usdjpy\",\"output\":\"phase:asia_open\"},{\"substrate\":\"audusd\",\"output\":\"phase:asia_open\"},{\"substrate\":\"usdcny\",\"output\":\"phase:asia_open\"}]"}'
    ;;

  india_open)
    # 09:15 IST — Feed India + Asia mid-session
    log "Feeding India open + Asia mid-session"
    post '{"action":"batch","items":"[{\"substrate\":\"nifty50\",\"output\":\"phase:india_open\"},{\"substrate\":\"banknifty\",\"output\":\"phase:india_open\"},{\"substrate\":\"sensex\",\"output\":\"phase:india_open\"}]"}'
    post '{"action":"batch","items":"[{\"substrate\":\"usdinr\",\"output\":\"phase:india_open\"},{\"substrate\":\"nikkei\",\"output\":\"phase:asia_mid\"},{\"substrate\":\"kospi\",\"output\":\"phase:asia_mid\"}]"}'
    ;;

  europe_open)
    # 13:30 IST — Feed Europe open + India mid-session
    log "Feeding Europe open + India mid"
    post '{"action":"batch","items":"[{\"substrate\":\"ftse\",\"output\":\"phase:europe_open\"},{\"substrate\":\"dax\",\"output\":\"phase:europe_open\"},{\"substrate\":\"cac40\",\"output\":\"phase:europe_open\"},{\"substrate\":\"eurostoxx\",\"output\":\"phase:europe_open\"}]"}'
    post '{"action":"batch","items":"[{\"substrate\":\"eurusd\",\"output\":\"phase:europe_open\"},{\"substrate\":\"gbpusd\",\"output\":\"phase:europe_open\"},{\"substrate\":\"nifty50\",\"output\":\"phase:india_mid\"}]"}'
    ;;

  india_close)
    # 15:30 IST — Feed India close + Europe mid
    log "Feeding India close + Europe mid"
    post '{"action":"batch","items":"[{\"substrate\":\"nifty50\",\"output\":\"phase:india_close\"},{\"substrate\":\"banknifty\",\"output\":\"phase:india_close\"},{\"substrate\":\"sensex\",\"output\":\"phase:india_close\"}]"}'
    post '{"action":"batch","items":"[{\"substrate\":\"dax\",\"output\":\"phase:europe_mid\"},{\"substrate\":\"ftse\",\"output\":\"phase:europe_mid\"},{\"substrate\":\"usdinr\",\"output\":\"phase:india_close\"}]"}'
    ;;

  us_open)
    # 19:00 IST — NYSE opens. London-NYSE overlap = max coupling.
    log "Feeding US open (max coupling window)"
    post '{"action":"batch","items":"[{\"substrate\":\"sp500\",\"output\":\"phase:us_open\"},{\"substrate\":\"nasdaq100\",\"output\":\"phase:us_open\"},{\"substrate\":\"russell2000\",\"output\":\"phase:us_open\"},{\"substrate\":\"dowjones\",\"output\":\"phase:us_open\"}]"}'
    post '{"action":"batch","items":"[{\"substrate\":\"vix\",\"output\":\"phase:us_open\"},{\"substrate\":\"dxy\",\"output\":\"phase:us_open\"},{\"substrate\":\"ftse\",\"output\":\"phase:london_nyse_overlap\"},{\"substrate\":\"dax\",\"output\":\"phase:london_nyse_overlap\"}]"}'
    ;;

  golden_hour)
    # 20:30 IST — All 4 rings touched. Maximum information state.
    # This is the full /globe equivalent — all 26 substrates.
    log "GOLDEN HOUR — full planetary feed"
    # All 15 indexes
    post '{"action":"batch","items":"[{\"substrate\":\"nikkei\",\"output\":\"phase:golden\"},{\"substrate\":\"kospi\",\"output\":\"phase:golden\"},{\"substrate\":\"hangseng\",\"output\":\"phase:golden\"},{\"substrate\":\"shanghai\",\"output\":\"phase:golden\"},{\"substrate\":\"asx200\",\"output\":\"phase:golden\"},{\"substrate\":\"nifty50\",\"output\":\"phase:golden\"},{\"substrate\":\"ftse\",\"output\":\"phase:golden\"},{\"substrate\":\"dax\",\"output\":\"phase:golden\"},{\"substrate\":\"cac40\",\"output\":\"phase:golden\"},{\"substrate\":\"eurostoxx\",\"output\":\"phase:golden\"},{\"substrate\":\"sp500\",\"output\":\"phase:golden\"},{\"substrate\":\"nasdaq100\",\"output\":\"phase:golden\"},{\"substrate\":\"russell2000\",\"output\":\"phase:golden\"},{\"substrate\":\"dowjones\",\"output\":\"phase:golden\"},{\"substrate\":\"bovespa\",\"output\":\"phase:golden\"}]"}'
    # All 6 currencies
    post '{"action":"batch","items":"[{\"substrate\":\"usdjpy\",\"output\":\"phase:golden\"},{\"substrate\":\"eurusd\",\"output\":\"phase:golden\"},{\"substrate\":\"gbpusd\",\"output\":\"phase:golden\"},{\"substrate\":\"usdcny\",\"output\":\"phase:golden\"},{\"substrate\":\"usdinr\",\"output\":\"phase:golden\"},{\"substrate\":\"audusd\",\"output\":\"phase:golden\"}]"}'
    # All 5 bridges
    post '{"action":"batch","items":"[{\"substrate\":\"es_futures\",\"output\":\"phase:golden\"},{\"substrate\":\"nq_futures\",\"output\":\"phase:golden\"},{\"substrate\":\"gold\",\"output\":\"phase:golden\"},{\"substrate\":\"oil_wti\",\"output\":\"phase:golden\"},{\"substrate\":\"bitcoin\",\"output\":\"phase:golden\"}]"}'
    # Save state after full feed
    post '{"action":"save"}'
    log "Full planetary feed complete + saved"
    ;;

  us_close)
    # 01:30 IST — NYSE closes. Bridges carry through dark window.
    log "Feeding US close + bridges for dark window"
    post '{"action":"batch","items":"[{\"substrate\":\"sp500\",\"output\":\"phase:us_close\"},{\"substrate\":\"nasdaq100\",\"output\":\"phase:us_close\"},{\"substrate\":\"russell2000\",\"output\":\"phase:us_close\"},{\"substrate\":\"dowjones\",\"output\":\"phase:us_close\"}]"}'
    post '{"action":"batch","items":"[{\"substrate\":\"es_futures\",\"output\":\"phase:dark_bridge\"},{\"substrate\":\"nq_futures\",\"output\":\"phase:dark_bridge\"},{\"substrate\":\"bitcoin\",\"output\":\"phase:dark_bridge\"},{\"substrate\":\"gold\",\"output\":\"phase:dark_bridge\"}]"}'
    # Save + snapshot before dark window
    post '{"action":"save"}'
    log "US close feed complete + saved for dark window"
    ;;

  *)
    echo "Usage: $0 <phase>"
    echo "  Phases: asia_open, india_open, europe_open, india_close, us_open, golden_hour, us_close"
    exit 1
    ;;

esac

log "END"
