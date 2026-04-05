import { tool } from "@opencode-ai/plugin"
import { execSync } from "child_process"

/*
 * webautomate — Browser authentication via standalone script.
 *
 * FIXED:
 *   1. Python is a SEPARATE SCRIPT (not embedded string)
 *   2. Auto-detects display → headed/headless/manual fallback
 *   3. Robust login detection (URL change + cookie change + password field gone)
 *   4. Structured errors with fix guidance
 *   5. Graceful degradation (Playwright → Xvfb → manual cookie import)
 */

export default tool({
  description:
    "Web login. Opens browser for you to type your password. " +
    "Auto-detects environment: GUI browser if display available, " +
    "manual cookie import if no display. Cookies captured and saved. " +
    "For browsing/fetching, use csos-core with url= instead.",
  args: {
    url: tool.schema.string().describe("Login page URL"),
    mode: tool.schema.enum(["auto", "headed", "headless", "manual"]).default("auto").describe(
      "auto: detect best mode. headed: GUI browser. headless: no GUI. manual: paste cookies."
    ),
  },
  async execute(args) {
    const modeFlag = args.mode === "auto" ? "" : `--${args.mode}`
    try {
      const result = execSync(
        `python3 scripts/csos-login.py ${JSON.stringify(args.url)} ${modeFlag}`.trim(),
        {
          encoding: "utf-8",
          timeout: 310000,
          env: { ...process.env },
          cwd: process.cwd(),
          stdio: ['inherit', 'pipe', 'pipe'],
        }
      ).trim()

      // Validate JSON response
      try {
        JSON.parse(result)
        return result
      } catch {
        return JSON.stringify({ error: true, message: "Invalid response from login script", raw: result.slice(0, 200) })
      }
    } catch (e: any) {
      const msg = e.message?.slice(0, 300) || "unknown error"

      // Structured error with fix guidance
      if (msg.includes("playwright")) {
        return JSON.stringify({
          error: true,
          message: "Playwright not installed",
          fix: "pip install playwright && playwright install chromium",
          fallback: "Use: webautomate url=... mode=manual (paste cookies from browser DevTools)"
        })
      }
      if (msg.includes("DISPLAY") || msg.includes("display")) {
        return JSON.stringify({
          error: true,
          message: "No display server available for GUI browser",
          fix: "Use: webautomate url=... mode=manual",
          detail: "In Docker/server environments, manual cookie import is the fallback."
        })
      }
      if (msg.includes("timeout") || msg.includes("Timeout")) {
        return JSON.stringify({
          error: true,
          message: "Login page did not load within 30 seconds",
          fix: "Check network connection. Verify the URL is correct."
        })
      }

      return JSON.stringify({ error: true, message: msg })
    }
  },
})
