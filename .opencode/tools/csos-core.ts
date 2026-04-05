import { tool } from "@opencode-ai/plugin"
import { spawn, execSync, ChildProcess } from "child_process"

/*
 * csos-core — Photosystem I.
 *
 * NO action enum. The tool infers from which args are present.
 * The args ARE the photon. The daemon resonates with the pattern.
 *
 * ABSORPTION SPECTRUM (which arg combinations trigger which chemistry):
 *   substrate + command  → exec: run bash, auto-absorb          (CLI interaction)
 *   substrate + output   → absorb: feed signal to 3-ring physics (signal processing)
 *   url                  → web: fetch URL, auto-absorb           (web interaction)
 *   ring                 → see: read ring state                   (state observation)
 *   key + value          → remember: store human data             (human memory)
 *   key (no value)       → recall: retrieve human data            (human recall)
 *   (no args)            → diagnose: check system health          (self-healing)
 *
 * 5 STRUCTURAL ELEMENTS (photosynthetic mapping):
 *   NAME:        "csos-core"              = Pigment identity (Chl a)
 *   DESCRIPTION: (below)                  = Absorption spectrum (Gouterman)
 *   ARGS:        substrate, command, url,  = Antenna complex (funnels intent)
 *                output, ring, key, value
 *   EXECUTE:     pipe(args) → daemon      = Reaction center (pure transduction)
 *   RETURN:      {data, physics}          = Electron output (carries energy + state)
 */

let d: ChildProcess | null = null
let ok = false

function boot() {
  if (d && !d.killed && ok) return

  // Try unified membrane binary first, then Python fallback
  const { existsSync } = require("fs")
  const nativePath = process.cwd() + "/csos"
  const useNative = existsSync(nativePath)

  try {
    if (useNative) {
      d = spawn(nativePath, [], {
        cwd: process.cwd(), stdio: ["pipe", "pipe", "pipe"], env: process.env
      })
    } else {
      d = spawn("python3", ["scripts/csos-daemon.py"], {
        cwd: process.cwd(), stdio: ["pipe", "pipe", "pipe"], env: process.env
      })
    }
    d.on("exit", () => { d = null; ok = false })
    ok = false; d.stdout!.once("data", () => { ok = true })
    const t = Date.now()
    while (!ok && Date.now() - t < 2000) execSync("sleep 0.05")
  } catch { d = null; ok = false }
}

function pipe(req: object): string {
  try {
    boot()
    if (d && ok) {
      let buf = ""
      return new Promise<string>(res => {
        const fn = (c: Buffer) => {
          buf += c.toString()
          const ln = buf.split("\n")
          if (ln.length > 1) { d!.stdout!.off("data", fn); res(ln[0]) }
        }
        d!.stdout!.on("data", fn)
        d!.stdin!.write(JSON.stringify(req) + "\n")
        setTimeout(() => { d!.stdout!.off("data", fn); res('{"error":"timeout"}') }, 15000)
      }) as unknown as string
    }
  } catch {}
  try {
    return execSync(
      `echo '${JSON.stringify(req).replace(/'/g, "\\'")}' | python3 scripts/csos-daemon.py`,
      { encoding: "utf-8", timeout: 30000, env: process.env }
    ).split("\n").filter(l => l.startsWith("{")).pop() || '{"error":"no output"}'
  } catch (e: any) {
    return JSON.stringify({ error: true, message: e.message?.slice(0, 200) })
  }
}

export default tool({
  description:
    "CSOS unified membrane. THE ONLY tool for all I/O. All data and output flows here. " +
    "20 actions auto-inferred from args: " +
    "command+substrate=exec CLI (auto-absorb). url=fetch web (auto-absorb). " +
    "output+substrate=absorb signal. content=deliver (auto-egress). " +
    "channel+payload=egress to specific channel. explain=reasoning. " +
    "ring=read state. key+value=remember. key only=recall. no args=diagnose. " +
    "Do NOT use write/edit/webfetch/websearch/bash tools — everything goes through csos-core.",
  args: {
    substrate: tool.schema.string().optional().describe("What you're operating on (databricks, github, codebase...)"),
    command: tool.schema.string().optional().describe("Shell command to execute and auto-absorb"),
    output: tool.schema.string().optional().describe("Raw tool output to absorb into physics"),
    url: tool.schema.string().optional().describe("URL to fetch and auto-absorb"),
    steps: tool.schema.string().optional().describe("JSON web steps: [{action:navigate/type/click/extract,...}]"),
    ring: tool.schema.string().optional().describe("Ring to read: eco_domain, eco_cockpit, eco_organism"),
    detail: tool.schema.string().optional().describe("Ring detail level: minimal, standard, cockpit, full"),
    key: tool.schema.string().optional().describe("Field name for remember/recall"),
    value: tool.schema.string().optional().describe("Field value for remember (omit to recall)"),
    signals: tool.schema.string().optional().describe("Comma-separated floats for direct fly"),
    items: tool.schema.string().optional().describe("JSON array of {substrate,output} for batch absorb"),
    channel: tool.schema.string().optional().describe("Egress channel: file, webhook, slack"),
    payload: tool.schema.string().optional().describe("Egress payload content"),
    path: tool.schema.string().optional().describe("File path for egress channel=file"),
    content: tool.schema.string().optional().describe("Deliverable content for auto-egress routing"),
    explain: tool.schema.string().optional().describe("Ring name to get human-readable reasoning explanation"),
    toolpath: tool.schema.string().optional().describe("Write to sanctioned path: .opencode/tools/*.ts, .opencode/agents/*.md, specs/*.csos"),
    body: tool.schema.string().optional().describe("File content for tool/agent/spec creation"),
    toolread: tool.schema.string().optional().describe("Read a file from sanctioned path"),
    toollist: tool.schema.string().optional().describe("List files in sanctioned directory"),
  },
  async execute(args) {
    // ═══ ANTENNA COMPLEX: infer action from which args are present ═══
    // No switch statement. The args pattern IS the photon wavelength.
    const req: any = {}

    if (args.toolpath && args.body) {
      req.action = "tool"; req.path = args.toolpath; req.body = args.body
    } else if (args.toolread) {
      req.action = "toolread"; req.path = args.toolread
    } else if (args.toollist) {
      req.action = "toollist"; req.dir = args.toollist
    } else if (args.content) {
      req.action = "deliver"; req.content = args.content
    } else if (args.explain) {
      req.action = "explain"; req.ring = args.explain
    } else if (args.channel && args.payload) {
      req.action = "egress"; req.channel = args.channel; req.payload = args.payload
      if (args.path) req.path = args.path
      if (args.url) req.url = args.url
    } else if (args.command && args.substrate) {
      req.action = "exec"; req.command = args.command; req.substrate = args.substrate
    } else if (args.url) {
      req.action = "web"; req.url = args.url
      if (args.substrate) req.substrate = args.substrate
      if (args.steps) req.steps = JSON.parse(args.steps)
    } else if (args.output && args.substrate) {
      req.action = "absorb"; req.substrate = args.substrate; req.output = args.output
    } else if (args.items) {
      req.action = "batch"; req.items = JSON.parse(args.items)
    } else if (args.ring && args.signals) {
      req.action = "fly"; req.ring = args.ring; req.signals = args.signals
    } else if (args.ring) {
      req.action = "see"; req.ring = args.ring; if (args.detail) req.detail = args.detail
    } else if (args.key && args.value) {
      req.action = "remember"; req.key = args.key; req.value = args.value
    } else if (args.key) {
      req.action = "recall"
    } else {
      req.action = "diagnose"
    }

    return pipe(req)
  },
})
