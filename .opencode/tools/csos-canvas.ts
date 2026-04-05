import { tool } from "@opencode-ai/plugin"
import { execSync, spawn } from "child_process"

/*
 * csos-canvas — Opens the spatial canvas connected to the ONE daemon.
 *
 * Starts daemon in HTTP mode if not running, opens browser.
 * Canvas connects via EventSource to /events. Live updates flow automatically.
 * No second brain. No subprocess management. No REST API duplication.
 *
 * ABSORPTION SPECTRUM:
 *   action=open     → start daemon with SSE if needed, open canvas
 *   action=status   → check if daemon SSE is reachable
 */

export default tool({
  description:
    "Opens the spatial canvas — a browser window into the daemon. " +
    "Starts daemon in HTTP mode (SSE on port 4200) if not running. " +
    "Canvas shows ring gauges, system topology, observation timeline. " +
    "Both TUI and canvas see the same daemon state. One brain.",
  args: {
    action: tool.schema.string().describe("open | status"),
    port: tool.schema.number().optional().describe("SSE port (default: 4200)"),
  },
  async execute(args) {
    const port = args.port || 4200

    if (args.action === "status") {
      try {
        const r = execSync(`curl -s -m 2 http://localhost:${port}/api/state`, { encoding: "utf-8", timeout: 3000 })
        const state = JSON.parse(r)
        return JSON.stringify({
          status: "running", port,
          rings: Object.keys(state.rings || {}).length,
          clients: state.clients || 0,
          url: `http://localhost:${port}`,
        })
      } catch {
        return JSON.stringify({ status: "not running", port })
      }
    }

    if (args.action === "open") {
      // Check if daemon SSE is already running
      let running = false
      try {
        execSync(`curl -s -m 1 http://localhost:${port}/api/state`, { encoding: "utf-8", timeout: 2000 })
        running = true
      } catch {}

      if (!running) {
        // Start daemon in HTTP mode
        const proc = spawn("python3", ["scripts/csos-daemon.py", "--sse"], {
          detached: true,
          stdio: "ignore",
          env: { ...process.env, CSOS_SSE: "1", CSOS_SSE_PORT: String(port) },
        })
        proc.unref()
        // Wait for it to boot
        execSync("sleep 2")
      }

      // Open browser
      try {
        execSync(`open http://localhost:${port} 2>/dev/null || xdg-open http://localhost:${port} 2>/dev/null || true`)
      } catch {}

      return JSON.stringify({
        action: "open",
        url: `http://localhost:${port}`,
        sse: `http://localhost:${port}/events`,
        api: `http://localhost:${port}/api/state`,
        command: `http://localhost:${port}/api/command (POST)`,
        note: "Canvas connects to the ONE daemon. TUI and canvas see the same state.",
      })
    }

    return JSON.stringify({ error: "Use action=open or action=status" })
  },
})
