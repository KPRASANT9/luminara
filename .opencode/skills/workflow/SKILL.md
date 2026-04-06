# Workflow Skill

Conversational workflow lifecycle through `@csos-living`. All interaction through OpenCode — the canvas is a passive observer.

## Commands

### Synthesize a workflow from description
```
csos-core workflow=synthesize description="fetch from API, parse JSON, validate schema, store in postgres, notify slack"
```
Returns: Mermaid spec + per-node metadata (processing_unit, libs, runtime, motor_strength)

### Draft from Mermaid spec
```
csos-core workflow=draft spec="fetch[Fetch] --> parse[Parse] --> store[Store]" name=my_pipeline
```
Returns: Parsed nodes with edges, hashes, motor strength

### Run a workflow
```
csos-core workflow=run spec="fetch --> parse --> store" name=my_pipeline
```
Returns: Per-stage Boyer decisions, deltas, motor strength. Auto-records job.

### Tab-complete substrates
```
csos-core workflow=complete prefix="pg"
```
Returns: Motor memory substrates + Calvin patterns + foundation atoms ranked by strength

### View job history
```
csos-core workflow=jobs
```

## Source Management

### Register a source with auth
```
csos-core auth=register sourceName=postgres_prod level=token
```

### Validate and get wrapper object
```
csos-core source=validate sourceName=postgres_prod
```
Returns: Typed wrapper with capabilities, auth_level, bind_as syntax for workflow nodes

### List all wrappers
```
csos-core source=wrappers
```

## Cluster Management

### Deploy workflow as cluster
```
csos-core cluster=create clusterId=etl_prod_v1 spec="fetch from postgres, validate, store s3"
```

### Check cluster status
```
csos-core cluster=status clusterId=etl_prod_v1
```

### List all clusters
```
csos-core cluster=list
```

## Node Configuration

### Configure a node with execution command
```
csos-core workflow=configure name=my_pipeline node=fetch config='{"command":"curl -sf https://api.example.com","timeout":"30"}'
```
Returns: Configured node with delta and decision

### Step through execution
```
csos-core workflow=run_step name=my_pipeline spec="fetch --> parse --> store" step=0
```
Returns: Real command output, exit code, physics (decision, delta, motor)

## Version Management

### List spec versions
```
csos-core workflow=versions name=my_pipeline
```

### Restore a previous version
```
csos-core workflow=restore name=my_pipeline version=2
```

## Execution Lifecycle

```
1. User describes intent → Agent synthesizes workflow
2. User previews Mermaid diagram in canvas (Visual mode)
3. User edits nodes interactively → Agent re-drafts
4. User switches to Code mode → Configures per-node commands
5. User switches to Exec mode → Runs workflow with real commands
6. Each node executes configured command via membrane
7. Physics tracks execution (Boyer decides, motor strengthens)
8. Results stream to canvas via SSE (per-node status, output, timing)
9. User iterates: edit spec → reconfigure → re-run
```

## Workflow Lifecycle

```
1. User describes intent in natural language
2. Agent synthesizes → Mermaid spec + compile/runtime metadata
3. User previews, requests edits → Agent re-synthesizes
4. Agent validates sources → Exposes as wrapper objects
5. Agent binds sources to nodes → Creates cluster
6. Cluster runs → Boyer decides per-stage
7. Agent monitors → Reports state via physics (gradient, speed, motor)
```

## Physics Integration

Every workflow action flows through the 5 equations:
- **Gouterman**: Routes signals to matching atoms (substrate hash → spectral range)
- **Marcus**: Error correction (predicted vs actual per stage)
- **Mitchell**: Evidence accumulation (gradient grows with successful stages)
- **Boyer**: Decision gate (speed > rw → EXECUTE the cluster)
- **Forster**: Cross-stage coupling (data flows between nodes)
- **Calvin**: Discovers patterns across workflow runs (synthesizes new atoms)
