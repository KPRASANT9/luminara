import { tool } from "@opencode-ai/plugin"
import { spawn, execSync, ChildProcess } from "child_process"

/*
 * csos-core — THE tool. Routes ALL I/O through the native ./csos binary.
 *
 * BIDIRECTIONAL DATA FLOW:
 *   1. Agent calls csos-core with args
 *   2. csos-core POSTs to HTTP daemon (if running) → SSE broadcasts to canvas
 *      OR pipes to stdin (if no daemon) → stdout back to agent
 *   3. Canvas sees EVERY agent action via SSE (bidirectional)
 *   4. User sees agent activity in real-time in the conversation panel
 *
 * This makes the response bidirectional:
 *   Agent → HTTP POST → Binary → SSE → Canvas (user sees it)
 *   Canvas → HTTP POST → Binary → SSE → Canvas (self-update)
 *   Both paths go through the SAME membrane_absorb() → same physics.
 *
 * 22 actions. Auto-inferred from args. Every response carries:
 *   decision (Boyer), delta (Mitchell), motor_strength (Forster).
 */

const HTTP_PORT = 4200

// Try HTTP first (bidirectional — canvas sees it via SSE)
function httpPost(req: object): string | null {
  try {
    return execSync(
      `curl -sf -m 5 -X POST http://localhost:${HTTP_PORT}/api/command ` +
      `-H 'Content-Type: application/json' ` +
      `-d '${JSON.stringify(req).replace(/'/g, "\\'")}'`,
      { encoding: "utf-8", timeout: 6000, cwd: process.cwd() }
    ).trim()
  } catch {
    return null
  }
}

// Fall back to stdin pipe (agent-only, canvas doesn't see it)
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

function pipeStdin(req: object): string {
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

function send(req: object): string {
  // HTTP first: bidirectional (canvas sees it via SSE)
  const httpResult = httpPost(req)
  if (httpResult) return httpResult
  // Fallback: stdin pipe (agent-only)
  return pipeStdin(req)
}

export default tool({
  description:
    "CSOS native membrane. THE ONLY tool for all I/O. " +
    "22 actions. Bidirectional: agent → binary → SSE → canvas (user sees everything). " +
    "command+substrate=exec. url=web. output+substrate=absorb. content=deliver. " +
    "channel+payload=egress. explain=reasoning. ring=see. key+value=remember. no args=diagnose. " +
    "Do NOT use write/edit/webfetch/websearch/bash — everything through csos-core.",
  args: {
    substrate: tool.schema.string().optional().describe("What you're operating on"),
    command: tool.schema.string().optional().describe("Shell command to exec + auto-absorb"),
    output: tool.schema.string().optional().describe("Raw data to absorb into membrane"),
    url: tool.schema.string().optional().describe("URL to fetch + auto-absorb"),
    steps: tool.schema.string().optional().describe("JSON web steps"),
    ring: tool.schema.string().optional().describe("Ring: eco_domain, eco_cockpit, eco_organism"),
    detail: tool.schema.string().optional().describe("Detail: minimal, standard, cockpit, full"),
    key: tool.schema.string().optional().describe("Field name for remember/recall"),
    value: tool.schema.string().optional().describe("Field value for remember"),
    signals: tool.schema.string().optional().describe("Comma-separated floats for fly"),
    items: tool.schema.string().optional().describe("JSON array for batch absorb"),
    channel: tool.schema.string().optional().describe("Egress channel: file, webhook, slack"),
    payload: tool.schema.string().optional().describe("Egress payload"),
    path: tool.schema.string().optional().describe("File path for egress"),
    content: tool.schema.string().optional().describe("Deliverable content"),
    explain: tool.schema.string().optional().describe("Ring name for reasoning"),
    toolpath: tool.schema.string().optional().describe("Write to sanctioned path"),
    body: tool.schema.string().optional().describe("File content for tool/spec"),
    toolread: tool.schema.string().optional().describe("Read from sanctioned path"),
    toollist: tool.schema.string().optional().describe("List sanctioned directory"),
    // Workflow drafting, execution, synthesis, jobs
    workflow: tool.schema.string().optional().describe("Workflow sub-action: draft, run, complete, synthesize, jobs"),
    spec: tool.schema.string().optional().describe("Mermaid-style workflow spec: A[label] --> B[label]"),
    description: tool.schema.string().optional().describe("Natural language description for synthesize"),
    name: tool.schema.string().optional().describe("Workflow name"),
    prefix: tool.schema.string().optional().describe("Completion prefix for tab-complete"),
    // Auth ladder for source registration
    auth: tool.schema.string().optional().describe("Auth sub-action: register, list, check"),
    source: tool.schema.string().optional().describe("Source sub-action: validate, wrappers"),
    sourceName: tool.schema.string().optional().describe("Source name for auth/validate"),
    level: tool.schema.string().optional().describe("Auth level: none, api_key, token, oauth"),
    // New args for workflow node configuration
    node: tool.schema.string().optional().describe("Node ID for configure action"),
    config: tool.schema.string().optional().describe("JSON config for node (command, env, timeout)"),
    step: tool.schema.string().optional().describe("Step index for run_step"),
    version: tool.schema.string().optional().describe("Version index for restore"),
    // Cluster management
    cluster: tool.schema.string().optional().describe("Cluster sub-action: create, status, list"),
    clusterId: tool.schema.string().optional().describe("Cluster instance ID"),
  },
  async execute(args) {
    const req: any = {}

    // Workflow actions
    if (args.workflow) {
      req.action = "workflow"; req.sub = args.workflow
      if (args.spec) req.spec = args.spec
      if (args.name) req.name = args.name
      if (args.prefix) req.prefix = args.prefix
      if (args.description) req.description = args.description
      // New fields for configure/run_step/versions
      if (args.node) req.node = args.node
      if (args.step) req.step = args.step
      if (args.version) req.version = args.version
      // Parse config JSON if provided
      if (args.config) {
        try { Object.assign(req, JSON.parse(args.config)); } catch {}
      }
    }
    // Cluster management
    else if (args.cluster) {
      req.action = "cluster"; req.sub = args.cluster
      if (args.clusterId) req.id = args.clusterId
      if (args.spec) req.spec = args.spec
    }
    // Source validation & wrappers
    else if (args.source) {
      req.action = "source"; req.sub = args.source
      if (args.sourceName) req.name = args.sourceName
    }
    // Auth actions
    else if (args.auth) {
      req.action = "auth"; req.sub = args.auth
      if (args.sourceName) req.source = args.sourceName
      if (args.level) req.level = args.level
    }
    else if (args.toolpath && args.body) {
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

    return send(req)
  },
})
