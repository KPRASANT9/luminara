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
async function httpPostAsync(req: object): Promise<string | null> {
  return new Promise((resolve) => {
    const data = JSON.stringify(req)
    const options = {
      hostname: 'localhost',
      port: HTTP_PORT,
      path: '/api/command',
      method: 'POST',
      headers: { 'Content-Type': 'application/json', 'Content-Length': Buffer.byteLength(data) },
      timeout: 5000,
    }
    const r = require('http').request(options, (res: any) => {
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

// Fall back to stdin pipe (agent-only, canvas doesn't see it)
let d: ChildProcess | null = null
let ok = false

function bootAsync(): Promise<void> {
  if (d && !d.killed && ok) return Promise.resolve()
  const nativePath = process.cwd() + "/csos"
  return new Promise((resolve) => {
    try {
      d = spawn(nativePath, [], {
        cwd: process.cwd(), stdio: ["pipe", "pipe", "pipe"], env: process.env
      })
      d.on("exit", () => { d = null; ok = false })
      ok = false
      const timeout = setTimeout(() => { resolve() }, 2000)
      d.stdout!.once("data", () => { ok = true; clearTimeout(timeout); resolve() })
    } catch { d = null; ok = false; resolve() }
  })
}

async function pipeStdin(req: object): Promise<string> {
  try {
    await bootAsync()
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

async function sendAsync(req: object): Promise<string> {
  // HTTP first: bidirectional (canvas sees it via SSE)
  const httpResult = await httpPostAsync(req)
  if (httpResult) return httpResult
  // Fallback: stdin pipe (agent-only)
  return pipeStdin(req)
}

export default tool({
  description:
    "THE ONLY tool. Routes ALL I/O through native ./csos binary. " +
    "27 actions: workflow(synthesize|draft|run|run_step|configure|complete|versions|restore|jobs), " +
    "ir(full|spec|compile|runtime), rdma(register|diffuse|status), " +
    "auth(register|list|check), source(validate|wrappers), cluster(create|status|list), " +
    "interact=reflexive(select|command|query|operate → eco_cockpit attention signal), " +
    "command+substrate=exec, url=web, output+substrate=absorb, content=deliver, " +
    "channel+payload=egress, explain=reasoning, ring=see, key+value=remember, no args=diagnose. " +
    "NEVER use write/edit/webfetch/websearch/bash.",
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
    // Universal IR
    ir: tool.schema.string().optional().describe("IR layer: full, spec, compile, runtime"),
    // RDMA operations
    rdma: tool.schema.string().optional().describe("RDMA sub-action: register, diffuse, status"),
    remoteRing: tool.schema.string().optional().describe("Remote ring name for RDMA diffuse"),
    nodeId: tool.schema.string().optional().describe("Remote node ID for RDMA"),
    // Session = Living Equation operations
    session: tool.schema.string().optional().describe("Session sub-action: list, observe, spawn, bind, schedule, tick, tick_all"),
    id: tool.schema.string().optional().describe("Session ID for observe/bind/schedule/tick"),
    binding: tool.schema.string().optional().describe("External binding (e.g. 'aws:cloudwatch', 'databricks:prod')"),
    ingress_type: tool.schema.string().optional().describe("Ingress type: command, url, pipe"),
    ingress_source: tool.schema.string().optional().describe("Ingress source: shell command or URL"),
    egress_type: tool.schema.string().optional().describe("Egress type: webhook, file, command"),
    egress_target: tool.schema.string().optional().describe("Egress target: URL, file path, or command"),
    interval: tool.schema.string().optional().describe("Schedule interval in seconds"),
    autonomous: tool.schema.string().optional().describe("Autonomous mode: true/false"),
    // Living equation observation
    equate: tool.schema.string().optional().describe("Show living equation: '' for all, or ring name"),
    // Reflexive loop: agent attention signals → eco_cockpit
    interact: tool.schema.string().optional().describe("Interaction type: select, command, query, operate"),
    target: tool.schema.string().optional().describe("Interaction target: ring name, session ID, or topic"),
  },
  async execute(args) {
    const req: any = {}

    // Session = Living Equation
    if (args.session) {
      req.action = "session"; req.sub = args.session
      if (args.id) req.id = args.id
      if (args.substrate) req.substrate = args.substrate
      if (args.binding) req.binding = args.binding
      if (args.ingress_type) req.ingress_type = args.ingress_type
      if (args.ingress_source) req.ingress_source = args.ingress_source
      if (args.egress_type) req.egress_type = args.egress_type
      if (args.egress_target) req.egress_target = args.egress_target
      if (args.interval) req.interval = args.interval
      if (args.autonomous) req.autonomous = args.autonomous
    }
    // Living equation view
    else if (args.equate !== undefined) {
      req.action = "equate"
      if (args.equate) req.ring = args.equate
    }
    // Reflexive loop: agent attention → eco_cockpit
    else if (args.interact) {
      req.action = "interact"
      req.type = args.interact
      if (args.target) req.target = args.target
      if (args.id) req.session = args.id
    }
    // Universal IR
    else if (args.ir) {
      req.action = "ir"; req.detail = args.ir
      if (args.spec) req.spec = args.spec
      if (args.name) req.name = args.name
    }
    // RDMA operations
    else if (args.rdma) {
      req.action = "rdma"; req.sub = args.rdma
      if (args.ring) req.ring = args.ring
      if (args.remoteRing) req.remote_ring = args.remoteRing
      if (args.nodeId) req.node = args.nodeId
    }
    // Workflow actions
    else if (args.workflow) {
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

    return await sendAsync(req)
  },
})
