# Workflow Skill

Conversational workflow lifecycle through `@csos-living`. All interaction through OpenCode.

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

## Substrate Onboarding (workflow → living equation)

A workflow is a substrate definition. Once a workflow runs successfully, bind it as a session for autonomous operation:

### 1. Describe → Synthesize
```
csos-core workflow=synthesize description="fetch orders from postgres, validate schema, push to S3"
```

### 2. Configure each node
```
csos-core workflow=configure name=order_pipeline node=pg_query config='{"command":"psql -h db.prod -c \"SELECT * FROM orders WHERE created > now() - interval 1 hour\""}'
csos-core workflow=configure name=order_pipeline node=validate config='{"command":"jq \"select(.amount > 0)\"","timeout":"10"}'
csos-core workflow=configure name=order_pipeline node=s3_io config='{"command":"aws s3 cp - s3://bucket/orders/$(date +%s).json"}'
```

### 3. Step-test each node
```
csos-core workflow=run_step name=order_pipeline spec="pg_query --> validate --> s3_io" step=0
csos-core workflow=run_step name=order_pipeline spec="pg_query --> validate --> s3_io" step=1
csos-core workflow=run_step name=order_pipeline spec="pg_query --> validate --> s3_io" step=2
```

### 4. Run full pipeline
```
csos-core workflow=run spec="pg_query --> validate --> s3_io" name=order_pipeline
```

### 5. Bind as living equation (session)
```
csos-core session=spawn id="order_pipeline" substrate="orders"
csos-core session=bind id="order_pipeline" binding="postgres:orders" ingress_type="command" ingress_source="psql -h db.prod -c 'SELECT count(*) FROM orders'" egress_type="file" egress_target=".csos/deliveries/order_pipeline.jsonl"
csos-core session=schedule id="order_pipeline" interval="300" autonomous="true"
```

The workflow → session pipeline: **describe → synthesize → configure → test → run → bind → schedule**

## Lifecycle

```
1. User describes intent → Agent synthesizes workflow
2. Agent shows spec + node table → suggests next steps conversationally
3. User says "configure fetch" → Agent configures the node
4. User says "run it" → Agent executes the workflow
5. Each node executes configured command via membrane
6. Physics tracks execution (Boyer decides, motor strengthens)
7. Agent presents results → suggests what to do next
8. User iterates: edit spec → reconfigure → re-run
9. When stable: bind as session → autonomous living equation
```

## Physics Integration

Every workflow action flows through the 5 equations:
- **Gouterman**: Routes signals to matching atoms (substrate hash → spectral range)
- **Marcus**: Error correction (predicted vs actual per stage)
- **Mitchell**: Evidence accumulation (gradient grows with successful stages)
- **Boyer**: Decision gate (speed > rw → EXECUTE the cluster)
- **Forster**: Cross-stage coupling (data flows between nodes)
- **Calvin**: Discovers patterns across workflow runs (synthesizes new atoms)

## Universal IR

### Get full IR (all 3 layers)
```
csos-core ir=full
```
Returns: spec layer (atoms, rings, mermaid) + compile layer (formulas, params, JIT, constants) + runtime layer (physics, motor, RDMA endpoints)

### Get specific layer
```
csos-core ir=spec
csos-core ir=compile
csos-core ir=runtime
```

## RDMA Operations

### Register ring for remote access
```
csos-core rdma=register ring=eco_organism
```

### Cross-node Forster coupling
```
csos-core rdma=diffuse ring=eco_domain remoteRing=eco_domain nodeId=1
```

### RDMA status
```
csos-core rdma=status
```

## IR Layers

| Layer | What it shows | Key Commands |
|-------|--------------|-------------|
| Spec | atoms, rings, Mermaid | synthesize, draft, versions, ir=spec |
| Compile | formulas, params, JIT | configure, ir=compile |
| Runtime | physics, motor, RDMA | run, run_step, rdma, cluster, ir=runtime |
