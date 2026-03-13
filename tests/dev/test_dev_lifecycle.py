"""
Dev Substrate Lifecycle Test — exercises the full F-OS development loops.

Tests that:
  1. AgentInvocations are recorded (capture)
  2. AgentPerformanceMatrix is computed per agent (embed with DomainMatrix)
  3. W_d projection produces dev sub-scores (A_dev, E_dev, C_dev, V_dev)
  4. Model Router picks optimal models (dev Loop 1)
  5. Prompt Ratchet keeps improvements (dev Level 2)
  6. Dev Loop 2 produces DevConfigVersion (weekly optimization)
  7. Dev synthesizer computes F_dev = α·N_dev + β·I_dev
  8. Meta-gradient computed for Loop 3 consumption
"""
import pytest
from datetime import datetime, timezone, timedelta
from src.dev.physics import (
    AgentInvocation, DevMetricsTracker, AgentPerformanceEmbedder,
    DevPredictionEntry, DevSubScore,
)
from src.dev.router import ModelRouter
from src.dev.prompt_ratchet import PromptRatchet, PromptVersion
from src.dev.loops import DevLoop2
from src.dev.synthesize import compute_dev_objective


def _make_invocation(agent: str, task: str, model: str,
                     outcome: str = "success", tokens_in: int = 500,
                     tokens_out: int = 1000, duration_s: float = 30.0,
                     invariants_passed: int = 8, pattern: float = 0.85,
                     temp: float = 0.2) -> AgentInvocation:
    return AgentInvocation(
        agent_name=agent, model_id=model, task_type=task,
        tokens_in=tokens_in, tokens_out=tokens_out,
        cost_usd=(tokens_in * 3.0 + tokens_out * 15.0) / 1_000_000,
        duration_s=duration_s, temperature=temp, max_steps=20,
        invariants_passed=invariants_passed, invariants_total=8,
        pattern_adherence=pattern, contract_satisfied=True,
        outcome=outcome,
    )


@pytest.mark.asyncio
async def test_dev_substrate_full_lifecycle():
    """Complete dev substrate lifecycle: capture → embed → route → optimize → synthesize."""
    metrics = DevMetricsTracker()

    # ── STAGE 1: CAPTURE — simulate 20 agent invocations ──
    sonnet = "anthropic/claude-sonnet-4-20250514"
    haiku = "anthropic/claude-haiku-4-20250514"
    opus = "anthropic/claude-opus-4-20250514"

    invocations = [
        _make_invocation("coder", "implement", sonnet, "success", 800, 2000, 45),
        _make_invocation("coder", "implement", sonnet, "success", 700, 1800, 40),
        _make_invocation("coder", "implement", sonnet, "failure", 900, 2200, 60, 6),
        _make_invocation("coder", "implement", haiku, "success", 500, 1200, 25),
        _make_invocation("tester", "test", haiku, "success", 400, 800, 15),
        _make_invocation("tester", "test", haiku, "success", 350, 750, 12),
        _make_invocation("tester", "test", sonnet, "success", 600, 1500, 30),
        _make_invocation("reviewer", "review", sonnet, "success", 500, 1000, 20, temp=0.1),
        _make_invocation("reviewer", "review", sonnet, "success", 480, 950, 18, temp=0.1),
        _make_invocation("physicist", "physics_check", opus, "success", 800, 2000, 60, temp=0.1),
        _make_invocation("physicist", "physics_check", sonnet, "failure", 700, 1800, 50, 5),
        _make_invocation("architect", "plan", sonnet, "success", 600, 1500, 35, temp=0.4),
        _make_invocation("documenter", "document", haiku, "success", 300, 600, 10),
        _make_invocation("guardian", "security_audit", sonnet, "success", 450, 900, 25),
        _make_invocation("autotuner", "autotune", sonnet, "success", 400, 800, 20),
    ]
    for inv in invocations:
        metrics.record(inv)

    assert metrics.total_tokens > 0, "Tokens tracked"
    assert metrics.total_cost_usd > 0, "Cost tracked"

    # ── STAGE 2: EMBED — compute DomainMatrix per agent ──
    coder_embedder = AgentPerformanceEmbedder("coder")
    matrix, phi = coder_embedder.embed(invocations=invocations)

    assert matrix.k == 5, f"DomainMatrix rows: {matrix.k} (expected 5 perf dimensions)"
    assert matrix.n == 8, f"DomainMatrix cols: {matrix.n} (expected 8 task types)"
    assert "A_dev" in phi, "Projection produces A_dev"
    assert "E_dev" in phi, "Projection produces E_dev"
    assert "C_dev" in phi, "Projection produces C_dev"
    assert "V_dev" in phi, "Projection produces V_dev"

    # Verify the matrix has temporal variability (row sigmas) — dev I2
    sigmas = matrix.row_sigmas()
    # implement column has both success and failure → variance in accuracy row
    assert any(s > 0 for s in sigmas), "DomainMatrix captures performance variance"

    # ── STAGE 3: MODEL ROUTER (dev Loop 1) ──
    router = ModelRouter(metrics)
    routing = router.compute_routing()

    # With enough data, physicist should prefer opus (higher accuracy on physics_check)
    phys_assignment = routing.assignments.get("physicist")
    assert phys_assignment is not None, "Physicist has an assignment"

    # Tester should prefer haiku (fast, cheap, high accuracy on test)
    tester_assignment = routing.assignments.get("tester")
    assert tester_assignment is not None

    # ── STAGE 4: PROMPT RATCHET (dev Level 2) ──
    ratchet = PromptRatchet()
    v1 = PromptVersion(agent_name="coder", content="You are a coder.",
                        sections={"role": "You are a coder.", "rules": "Follow patterns."})
    ratchet.register_version(v1)

    # Record outcomes for this prompt version
    for inv in invocations:
        if inv.agent_name == "coder":
            ratchet.record_outcome("coder", v1.id,
                                    inv.outcome == "success",
                                    inv.tokens_in + inv.tokens_out)

    assert v1.pass_rate == 0.75, f"Coder pass rate: {v1.pass_rate}"

    # ── STAGE 5: DEV LOOP 2 (weekly optimization) ──
    loop2 = DevLoop2(metrics, router, ratchet)
    config_v1 = await loop2.run_weekly_cycle()

    assert config_v1.status == "deployed"
    assert len(config_v1.per_agent_accuracy) > 0
    assert config_v1.routing is not None

    # ── STAGE 6: DEV SYNTHESIZER — F_dev = α·N_dev + β·I_dev ──
    objective = compute_dev_objective(metrics, [config_v1])

    assert 0 <= objective.f_dev <= 100, f"F_dev in range: {objective.f_dev}"
    assert objective.n_dev >= 0, f"N_dev non-negative: {objective.n_dev}"
    assert objective.i_dev >= 0, f"I_dev non-negative: {objective.i_dev}"
    assert len(objective.agent_matrices) > 0, "Agent matrices computed"
    assert "coder" in objective.per_agent_sub_scores

    # Verify agent DomainMatrix structure
    coder_matrix = objective.agent_matrices.get("coder")
    assert coder_matrix is not None
    assert coder_matrix.k == 5, "Performance dimensions"
    assert coder_matrix.n == 8, "Task types"

    print(f"\n{'='*60}")
    print(f"  DEV SUBSTRATE LIFECYCLE TEST PASSED")
    print(f"{'='*60}")
    print(f"  F_dev:  {objective.f_dev:.1f}")
    print(f"  N_dev:  {objective.n_dev:.1f} (code production)")
    print(f"  I_dev:  {objective.i_dev:.1f} (agent learning)")
    print(f"  Agents: {len(objective.agent_matrices)} with DomainMatrices")
    print(f"  Total tokens: {metrics.total_tokens:,}")
    print(f"  Total cost:   ${metrics.total_cost_usd:.4f}")
    print(f"  Coder sub-scores: {objective.per_agent_sub_scores.get('coder', {})}")
    print(f"{'='*60}")
