# /optimize-agents — Run Dev Substrate Optimization Loops

Execute the three self-improving loops for the agent swarm.

## Instructions

This command runs the same three-loop architecture as HAPOS, applied to agents:

### Loop 1: Model Router (immediate)
- Read all agent invocation metrics
- Compute DomainMatrix per agent (5 perf dims × 8 task types)
- Score each model candidate per agent×task pair
- Assign optimal model, temperature, max_steps per agent
- Output: Updated RoutingTable

### Loop 2: Config Optimization (weekly)
- Compute meta-gradient: which agents are accelerating/decelerating?
- Run Prompt Ratchet: evaluate and refine agent prompts
- Gate: Dev I7 — accuracy non-decreasing per agent
- Output: DevConfigVersion

### Loop 3: Architecture Evolution (monthly)
- Analyze 4+ weeks of Loop 2 history
- Propose structural changes (merge/split/reroute agents)
- Gate: Dev I8 — accuracy-cost ratio must improve
- Output: DevArchitectureVersion with change proposals

Target: $LEVEL (1=router | 2=config | 3=architecture | all)

RUN echo "Optimizing agents at level: $LEVEL"
