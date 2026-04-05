# /dev-status — Development Substrate Health Dashboard

Show the F-OS development objective and per-agent performance.

## Instructions

Display:
1. F_dev = α·N_dev + β·I_dev (unified development score)
2. N_dev breakdown: accuracy, efficiency, coherence, velocity across all agents
3. I_dev: agent learning rate (accuracy improvement trend)
4. Per-agent DomainMatrix summary: top performer and bottleneck per task type
5. Model assignments: which model is assigned to which agent and why
6. Cost tracking: total tokens, total cost, cost per successful commit
7. Meta-gradient: which agents are accelerating/decelerating
8. Prompt Ratchet status: pass rate per prompt version, pending modifications

Format as a 3×3 matrix showing dev substrate health:

|               | L1: Execute      | L2: Learn          | L3: Meta-Learn      |
|---------------|-----------------|--------------------|--------------------|
| S2-Code       | Invariant pass% | Accuracy trend     | Architecture health |
| S3-Params     | Router scores   | Config improvement | Topology changes    |
| S3-Prompts    | Prompt pass%    | Ratchet keep rate  | Section analysis    |

RUN echo "Dev substrate status"
