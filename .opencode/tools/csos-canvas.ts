import { tool } from "@opencode-ai/plugin"
import { execSync, spawn } from "child_process"

/*
 * csos-canvas — Opens the CSOS workflow canvas (native binary daemon).
 * Single process. Zero Python. Spec-driven. LLVM JIT.
 */

export default tool({
  description:
    "Opens the CSOS workflow canvas served by the native binary daemon. " +
    "Single binary: physics + HTTP + SSE + canvas. Spec-driven. LLVM JIT.",
  args: {
    action: tool.schema.string().describe("open | status"),
    port: tool.schema.number().optional().describe("HTTP port (default: 4200)"),
  },
  async execute(args) {
    const port = args.port || 4200

    if (args.action === "status") {
      try {
        const r = execSync(`curl -s -m 2 http://localhost:${port}/api/state`, { encoding: "utf-8", timeout: 3000 })
        const state = JSON.parse(r)
        return JSON.stringify({ status: "running", port, native: state.native,
          rings: Object.keys(state.rings || {}).length, url: `http://localhost:${port}` })
      } catch {
        return JSON.stringify({ status: "not running", port })
      }
    }

    if (args.action === "open") {
      let running = false
      try {
        execSync(`curl -s -m 1 http://localhost:${port}/api/state`, { encoding: "utf-8", timeout: 2000 })
        running = true
      } catch {}

      if (!running) {
        const proc = spawn("./csos", ["--http", String(port)], {
          detached: true, stdio: "ignore",
        })
        proc.unref()
        execSync("sleep 1")
      }

      try {
        execSync(`open http://localhost:${port} 2>/dev/null || xdg-open http://localhost:${port} 2>/dev/null || true`)
      } catch {}

      return JSON.stringify({
        action: "open", url: `http://localhost:${port}`,
        native: true, note: "Single binary daemon. Zero Python.",
      })
    }

    return JSON.stringify({ error: "Use action=open or action=status" })
  },
})
