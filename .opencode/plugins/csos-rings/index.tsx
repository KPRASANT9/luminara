/**
 * csos-rings — OpenCode TUI plugin
 *
 * Synthesizes eco_domain, eco_cockpit, eco_organism physics into
 * human-readable sidebar content. No raw numbers — just meaning.
 *
 * Data flow (4 sources, cascading):
 *   1. Boot: GET localhost:4200/api/state
 *   2. Disk: .csos/rings/_latest.json (written by server.ts)
 *   3. SSE:  EventSource("/events") for real-time
 *   4. Event bus: "message.part.updated" on ToolPart completion
 *
 * Slots:
 *   sidebar_content       — 3-ring human interpretation
 *   sidebar_footer        — one-line system state
 *   session_prompt_right  — compact readiness indicator
 */

import type { TuiPluginModule, TuiPluginApi } from "@opencode-ai/plugin/tui";

// ── Types ──

type Ring = {
  gradient: number;
  speed: number;
  F?: number;
  rw?: number;
  decision?: string;
};

type Physics = {
  decision: string;
  delta: number;
  motor_strength: number;
  domain?: Ring;
  cockpit?: Ring;
  organism?: Ring;
};

// ── Constants (from lib/membrane.h) ──

const BOYER_THRESHOLD = 0.3;
const DEFAULT_RW = 0.8333;
const CALVIN_GRAD_FRAC = 0.05;

// ── Parse photon JSON into Physics ──

function parseRings(data: any): Partial<Physics> | null {
  try {
    if (typeof data === "string") data = JSON.parse(data);
    const result: Partial<Physics> = {};

    if (data.decision) result.decision = data.decision;
    if (data.delta !== undefined) result.delta = data.delta;
    if (data.motor_strength !== undefined)
      result.motor_strength = data.motor_strength;

    const rings = data.rings || data;
    for (const key of ["domain", "cockpit", "organism"] as const) {
      const d = rings[key] || rings[`eco_${key}`];
      if (d && typeof d === "object") {
        result[key] = {
          gradient: d.gradient ?? d.grad ?? 0,
          speed: d.speed ?? d.spd ?? 0,
          F: d.F,
          rw: d.rw ?? DEFAULT_RW,
          decision: d.decision,
        };
      }
    }

    return Object.keys(result).length > 0 ? result : null;
  } catch {
    return null;
  }
}

// ── Human synthesis ──
// Every phrase maps to a physics condition from the 5 equations.

function ringLabel(key: string): string {
  switch (key) {
    case "domain":   return "Focus";     // Gouterman: what resonates
    case "cockpit":  return "Process";   // Mitchell: how gradient accumulates
    case "organism": return "System";    // Boyer: overall decision gate
    default:         return key;
  }
}

function ringState(r: Ring): string {
  const confidence = r.rw && r.rw > 0 ? r.speed / r.rw : 0;

  if (r.decision === "EXECUTE") {
    if (confidence > 2) return "Strong signal, ready";
    return "Enough evidence, ready";
  }
  if (r.decision === "ASK") return "Needs your input";
  if (r.decision === "STORE") return "Blocked, queued";

  // EXPLORE states
  if (r.gradient < CALVIN_GRAD_FRAC) return "No signal yet";
  if (r.speed < BOYER_THRESHOLD) return "Gathering evidence";
  if (confidence > 0.5) return "Building confidence";
  return "Learning";
}

function motorPhrase(m: number): string {
  if (m >= 0.8) return "Well-known territory";
  if (m >= 0.5) return "Familiar";
  if (m >= 0.2) return "Getting familiar";
  if (m > 0)    return "New territory";
  return "First encounter";
}

function deltaPhrase(d: number): string {
  if (d > 50)  return "Strong progress";
  if (d > 10)  return "Making progress";
  if (d > 0)   return "Slight progress";
  if (d === 0) return "No change";
  return "Regressing";
}

function overallState(p: Physics): string {
  const d = p.delta;
  const m = p.motor_strength;

  if (p.decision === "EXECUTE" && m >= 0.8) return "Confident and ready";
  if (p.decision === "EXECUTE" && m >= 0.4) return "Ready to act";
  if (p.decision === "EXECUTE")             return "Ready, new substrate";
  if (p.decision === "ASK")                 return "Waiting for input";
  if (p.decision === "STORE")               return "Parked";
  if (d > 0 && m >= 0.5) return "On track";
  if (d > 0)             return "Learning, progressing";
  if (d === 0 && m < 0.2) return "Exploring new ground";
  if (d === 0)           return "Stalled";
  return "Investigating";
}

function readinessIcon(p: Physics): string {
  switch (p.decision) {
    case "EXECUTE": return "\u25cf";  // ● go
    case "EXPLORE": return "\u25cb";  // ○ thinking
    case "ASK":     return "\u25c6";  // ◆ needs input
    case "STORE":   return "\u25a1";  // □ parked
    default:        return "\u2022";  // •
  }
}

// ── Plugin ──

const KV_KEY = "csos.rings";

const plugin: TuiPluginModule = {
  id: "csos-rings",
  tui: async (api: TuiPluginApi) => {

    const defaults: Physics = {
      decision: "\u2014", delta: 0, motor_strength: 0,
    };

    function get(): Physics {
      return api.kv.get<Physics>(KV_KEY, defaults);
    }

    function set(partial: Partial<Physics>) {
      api.kv.set(KV_KEY, { ...get(), ...partial });
    }

    // ── Source 1: HTTP daemon state ──
    try {
      const res = await fetch("http://localhost:4200/api/state");
      const state = await res.json();
      const parsed = parseRings(state);
      if (parsed) set(parsed);
    } catch {
      // ── Source 2: Disk fallback ──
      try {
        const fs = await import("fs");
        const path = await import("path");
        const file = path.join(process.cwd(), ".csos/rings/_latest.json");
        const raw = fs.readFileSync(file, "utf-8");
        const parsed = parseRings(raw);
        if (parsed) set(parsed);
      } catch {}
    }

    // ── Source 3: SSE real-time stream ──
    let es: EventSource | null = null;
    try {
      if (typeof EventSource !== "undefined") {
        es = new EventSource("http://localhost:4200/events");
        es.addEventListener("response", (e: any) => {
          const parsed = parseRings(e.data);
          if (parsed) set(parsed);
        });
        es.addEventListener("state", (e: any) => {
          const parsed = parseRings(e.data);
          if (parsed) set(parsed);
        });
      }
    } catch {}
    api.lifecycle.onDispose(() => { es?.close(); });

    // ── Source 4: Event bus — ToolPart completion ──
    // Event type: "message.part.updated" (from SDK Event union)
    // Part type: ToolPart { tool: "csos-core", state: ToolStateCompleted }
    api.event.on("message.part.updated", (evt: any) => {
      const part = evt.properties?.part || evt.part;
      if (!part) return;
      if (part.type !== "tool") return;
      if (part.tool !== "csos-core") return;
      if (part.state?.status !== "completed") return;

      const output = part.state.output;
      if (!output) return;

      const parsed = parseRings(output);
      if (parsed) set(parsed);
    });

    // ── Slot: sidebar_content — 3-ring human state ──
    api.slots.register({
      slots: {
        sidebar_content: (_props: { session_id: string }) => {
          const p = get();

          const rings: [string, Ring | undefined][] = [
            ["domain", p.domain],
            ["cockpit", p.cockpit],
            ["organism", p.organism],
          ];

          const lines: string[] = [];
          for (const [key, ring] of rings) {
            const label = ringLabel(key);
            if (ring) {
              const icon = ring.decision === "EXECUTE" ? "\u2713" : "\u25cb";
              lines.push(`${icon} ${label}`);
              lines.push(`  ${ringState(ring)}`);
            } else {
              lines.push(`\u2014 ${label}`);
              lines.push("  Waiting for signal");
            }
          }

          lines.push("");
          lines.push(motorPhrase(p.motor_strength));
          lines.push(deltaPhrase(p.delta));

          return (<text>{lines.join("\n")}</text>) as any;
        },

        sidebar_footer: (_props: { session_id: string }) => {
          const p = get();
          return (<text>{overallState(p)}</text>) as any;
        },

        session_prompt_right: (_props: { session_id: string }) => {
          const p = get();
          const icon = readinessIcon(p);
          const word = p.decision === "EXECUTE" ? "ready"
                     : p.decision === "ASK"     ? "ask"
                     : p.decision === "STORE"   ? "parked"
                     : "exploring";
          return (<text>{`${icon} ${word}`}</text>) as any;
        },
      },
    });
  },
};

export default plugin;
