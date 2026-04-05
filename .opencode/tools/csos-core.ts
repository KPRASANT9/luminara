import { tool } from "@opencode-ai/plugin"
import { spawn, execSync, ChildProcess } from "child_process"

/*
 * csos-core — THE tool. Spawns ./csos native binary (95KB, LLVM JIT).
 *
 * 22 actions auto-inferred from args. No action enum.
 * The args ARE the photon. The membrane resonates with the pattern.
 *
 * ABSORPTION SPECTRUM (which args trigger which action):
 *
 *   PHYSICS:
 *     substrate + output   → absorb: feed signal through membrane (3 compartments)
 *     substrate + command  → exec: run CLI + auto-absorb stdout (Law I enforced)
 *     url                  → web: fetch URL + auto-absorb response
 *     ring + signals       → fly: direct signal injection into ring
 *     ring                 → see: read ring state (minimal/standard/cockpit/full)
 *     items (JSON array)   → batch: multiple absorb in one call
 *
 *   MEMORY:
 *     key + value          → remember: store human data
 *     key (no value)       → recall: retrieve stored data
 *
 *   DELIVERY:
 *     content              → deliver: auto-route output
 *     channel + payload    → egress: specific channel (file/webhook/slack)
 *     explain              → explain: human-readable physics reasoning
 *
 *   TOOLS:
 *     toolpath + body      → tool: write to sanctioned path
 *     toolread             → toolread: read from sanctioned path
 *     toollist             → toollist: list sanctioned directory
 *
 *   DIAGNOSTICS:
 *     (no args)            → diagnose: system health check
 *
 *   ADDITIONAL (via direct JSON pipe):
 *     grow, diffuse, lint, ping, muscle, hash, profile, save
 *
 * Every response carries: decision (Boyer), delta (Mitchell), motor_strength (Forster).
 * The membrane computes ALL physics — Gouterman → Marcus → Mitchell → Boyer → Calvin.
 * The LLM reads 3 numbers and knows what to do next.
 */

let d: ChildProcess | null = null
let ok = false

function boot() {
  if (d && !d.killed && ok) return
  const nativePath = process.cwd() + "/csos"
  try {
    d = spawn(nativePath, [], {
      cwd: process.cwd(), stdio: ["pipe", "pipe", "pipe"], env: process.env
    })
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
      `echo '${JSON.stringify(req).replace(/'/g, "\\'")}' | ./csos`,
      { encoding: "utf-8", timeout: 30000, cwd: process.cwd() }
    ).split("\n").filter(l => l.startsWith("{")).pop() || '{"error":"no output"}'
  } catch (e: any) {
    return JSON.stringify({ error: true, message: e.message?.slice(0, 200) })
  }
}

export default tool({
  description:
    "CSOS native membrane. THE ONLY tool for all I/O. Spawns ./csos binary (LLVM JIT). " +
    "22 actions auto-inferred from args. Every response carries decision + delta + motor_strength. " +
    "command+substrate=exec. url=web. output+substrate=absorb. content=deliver. " +
    "channel+payload=egress. explain=reasoning. ring=see. key+value=remember. no args=diagnose. " +
    "Physics: Gouterman(spectral) → Marcus(error) → Mitchell(gradient) → Boyer(decision) → Calvin(synthesis). " +
    "Do NOT use write/edit/webfetch/websearch/bash — everything through csos-core.",
  args: {
    substrate: tool.schema.string().optional().describe("What you're operating on"),
    command: tool.schema.string().optional().describe("Shell command to exec + auto-absorb"),
    output: tool.schema.string().optional().describe("Raw data to absorb into membrane"),
    url: tool.schema.string().optional().describe("URL to fetch + auto-absorb"),
    steps: tool.schema.string().optional().describe("JSON web steps: [{action:navigate/type/click/extract,...}]"),
    ring: tool.schema.string().optional().describe("Ring: eco_domain, eco_cockpit, eco_organism"),
    detail: tool.schema.string().optional().describe("Detail: minimal, standard, cockpit, full"),
    key: tool.schema.string().optional().describe("Field name for remember/recall"),
    value: tool.schema.string().optional().describe("Field value for remember (omit = recall)"),
    signals: tool.schema.string().optional().describe("Comma-separated floats for fly"),
    items: tool.schema.string().optional().describe("JSON array of {substrate,output} for batch"),
    channel: tool.schema.string().optional().describe("Egress channel: file, webhook, slack"),
    payload: tool.schema.string().optional().describe("Egress payload"),
    path: tool.schema.string().optional().describe("File path for egress channel=file"),
    content: tool.schema.string().optional().describe("Deliverable content (auto-egress)"),
    explain: tool.schema.string().optional().describe("Ring name for reasoning explanation"),
    toolpath: tool.schema.string().optional().describe("Write to sanctioned path"),
    body: tool.schema.string().optional().describe("File content for tool/spec creation"),
    toolread: tool.schema.string().optional().describe("Read from sanctioned path"),
    toollist: tool.schema.string().optional().describe("List sanctioned directory"),
  },
  async execute(args) {
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
