# Spec-Driven Development

> Reference documentation. Not an agent. The @csos-living agent consults this skill.

## Principle

Mermaid diagrams are the formal input. The diagram IS the spec. Edit the diagram, the spec changes. The agent reads the spec, not free-form prompts.

## Workflow

```
1. Human draws Mermaid diagram (or edits .csos spec directly)
2. Agent reads spec: read specs/[name].csos
3. Agent absorbs: csos-core substrate=specdriven output="[spec content]"
4. Physics decides: EXECUTE → act on spec. EXPLORE → ask one question.
5. Canvas renders: daemon SSE pushes absorb events → canvas updates live.
```

## .csos Spec Format

```csos
atom name {
    formula: equation;
    source:  "reference";
    params:  { key: value };
    role:    "What question does this answer?";
}

ring name {
    atoms: [atom1, atom2];
    purpose: "What signals does this ring absorb?";
}

agent name {
    mode: read_only | read_write | full;
    loop { ... }
}

law name {
    enforcement: "What is being enforced?";
    enforced_by: ["mechanism"];
}
```

## Data Integrations as Diagram Nodes

Each integration is a node in the diagram and an atom in the spec:

| Diagram Node | Becomes | Role |
|---|---|---|
| `DB[(Postgres)]` | `atom postgres_ingest` | Absorb signals from PostgreSQL |
| `KF((Kafka))` | `atom kafka_ingest` | Absorb streaming signals |
| `API[REST API]` | `atom rest_ingest` | Absorb signals from REST endpoints |

## Canvas

The canvas is a VIEW into the daemon (not a separate process):
- `csos-canvas action=start` — starts SSE on daemon, serves HTML
- Canvas connects to `localhost:4200/sse`
- Every `absorb` in the daemon pushes ring state to canvas
- Click rings in sidebar → adds live gauge to canvas

## What This Skill Is NOT

- NOT an agent. @csos-living handles everything.
- NOT a build tool. No code generation, no worktree cloning.
- NOT a separate process. One daemon, one brain.
