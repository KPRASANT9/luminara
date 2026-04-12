import { tool } from "@opencode-ai/plugin"
import { spawn, execSync, ChildProcess } from "child_process"

/*
 * csos — THE tool. Zero dispatch. Zero if-else.
 *
 * V12 had 2 tools with 50 lines of routing logic.
 * V13 has 1 tool with 0 lines of routing logic.
 *
 * The binary infers action from which fields are present.
 * The membrane routes by hash, not by strcmp.
 * F→0 for tools: COMPLEXITY = 0 routing lines. ACCURACY = binary decides.
 */

const HTTP_PORT = 4200

async function httpPost(req: object): Promise<string | null> {
  return new Promise((resolve) => {
    const data = JSON.stringify(req)
    const r = require('http').request({
      hostname: 'localhost', port: HTTP_PORT, path: '/api/command',
      method: 'POST', timeout: 5000,
      headers: { 'Content-Type': 'application/json', 'Content-Length': Buffer.byteLength(data) },
    }, (res: any) => {
      let body = ''
      res.on('data', (c: Buffer) => { body += c.toString() })
      res.on('end', () => resolve(body.trim() || null))
    })
    r.on('error', () => resolve(null))
    r.on('timeout', () => { r.destroy(); resolve(null) })
    r.write(data)
    r.end()
  })
}

let d: ChildProcess | null = null
let ok = false

function boot(): Promise<void> {
  if (d && !d.killed && ok) return Promise.resolve()
  return new Promise((resolve) => {
    try {
      d = spawn(process.cwd() + "/csos", [], {
        cwd: process.cwd(), stdio: ["pipe", "pipe", "pipe"], env: process.env
      })
      d.on("exit", () => { d = null; ok = false })
      ok = false
      const t = setTimeout(() => resolve(), 2000)
      d.stdout!.once("data", () => { ok = true; clearTimeout(t); resolve() })
    } catch { d = null; ok = false; resolve() }
  })
}

async function pipe(req: object): Promise<string> {
  try {
    await boot()
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
      })
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

async function send(req: object): Promise<string> {
  const r = await httpPost(req)
  if (r) return r
  return pipe(req)
}

/*
 * THE TOOL — Zero dispatch logic.
 *
 * Every arg becomes a JSON field. The binary infers the action:
 *   substrate + output  → absorb
 *   ring                → see
 *   content             → deliver
 *   command + substrate → exec
 *   equate              → equate
 *   explain             → explain
 *   (nothing)           → diagnose
 *
 * The binary handles this inference in protocol.c dispatch_infer().
 * This file does ZERO routing. It's a wire.
 */
export default tool({
  description:
    "THE ONLY tool. All args become JSON fields sent to ./csos. " +
    "The binary infers action from field presence. Zero routing logic here. " +
    "substrate+output=absorb, ring=see, content=deliver, equate=vitality, (empty)=diagnose. " +
    "Market signals: substrate=equity_tick output='SPY 523.41 bid=523.40 ask=523.42'. " +
    "Same membrane. Same 5 equations. Same physics.",
  args: {
    action: tool.schema.string().optional().describe("Explicit action override. Usually inferred."),
    substrate: tool.schema.string().optional().describe("Signal source name"),
    output: tool.schema.string().optional().describe("Signal data to absorb"),
    ring: tool.schema.string().optional().describe("Ring to query: eco_domain, eco_cockpit, eco_organism"),
    detail: tool.schema.string().optional().describe("Detail level: minimal, standard, cockpit, full"),
    signals: tool.schema.string().optional().describe("Comma-separated floats for fly"),
    command: tool.schema.string().optional().describe("Shell command to exec + auto-absorb"),
    url: tool.schema.string().optional().describe("URL to fetch + auto-absorb"),
    key: tool.schema.string().optional().describe("Key for remember/recall"),
    value: tool.schema.string().optional().describe("Value for remember"),
    content: tool.schema.string().optional().describe("Deliverable content"),
    explain: tool.schema.string().optional().describe("Ring name for reasoning"),
    equate: tool.schema.string().optional().describe("'' for all, or ring name"),
    toolpath: tool.schema.string().optional().describe("Write to sanctioned path"),
    body: tool.schema.string().optional().describe("File content for tool write"),
    toolread: tool.schema.string().optional().describe("Read from sanctioned path"),
    toollist: tool.schema.string().optional().describe("List sanctioned directory"),
    channel: tool.schema.string().optional().describe("Egress channel: file, webhook"),
    payload: tool.schema.string().optional().describe("Egress payload"),
    path: tool.schema.string().optional().describe("File path for egress"),
    intent: tool.schema.string().optional().describe("Intent text for route action"),
    symbol: tool.schema.string().optional().describe("Ticker for market signals"),
    price: tool.schema.string().optional().describe("Price for market tick"),
    bid: tool.schema.string().optional().describe("Best bid"),
    ask: tool.schema.string().optional().describe("Best ask"),
    volume: tool.schema.string().optional().describe("Period volume"),
  },
  async execute(args) {
    /* Pass ALL args through. The binary decides. */
    const req: any = {}
    for (const [k, v] of Object.entries(args)) {
      if (v !== undefined && v !== null && v !== '') req[k] = v
    }
    return await send(req)
  },
})
