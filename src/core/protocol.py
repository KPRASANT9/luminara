"""
Universal Agent Communication Protocol.

9 agent roles → 9 cells of the 3×3 matrix.
Every agent has exactly ONE input type and ONE output type.
Communication is via NATS events with sub_score_targets.

This protocol is domain-agnostic. It works for HAPOS (health),
RetailOS (retail), EduOS (education), MfgOS (manufacturing).
The agent ROLES are universal. The IMPLEMENTATIONS differ per domain.

The communication graph:
  capture → canonicalize → embed → couple → synthesize → feedback
       ↑                                           ↓
       └──── loop2 ←── loop3 ←── autotune ←── audit

Each arrow is a StageOutput with typed payload.
"""
from __future__ import annotations
from dataclasses import dataclass, field
from datetime import datetime, timezone
from typing import Any, Protocol
from enum import Enum
import uuid
import numpy as np


class AgentRole(str, Enum):
    """The 9 universal agent roles. Same for every F-OS instantiation."""
    CAPTURER = "capturer"           # S1-L1: raw data → events
    CANONICALIZER = "canonicalizer" # S1-L1: events → aggregates
    EMBEDDER = "embedder"           # S1-L1: aggregates → domain matrices
    COUPLER = "coupler"             # S1-L1: matrices → cross-domain Κ
    SYNTHESIZER = "synthesizer"     # S1-L1: Φ + Κ + Π → F + ∇F + ∇²F
    FEEDBACK = "feedback"           # S1-L1: predictions → outcomes (closes loop)
    REFINER = "refiner"             # S1-L2: params + embeddings update (Loop 2)
    TUNER = "tuner"                 # S1-L3: precision + policy + horizon (Loop 3)
    AUDITOR = "auditor"             # Cross: invariants + I(t) > 0 check


@dataclass
class Message:
    """
    The universal inter-agent message.
    Every message flows through NATS with these fields.
    """
    id: str = field(default_factory=lambda: str(uuid.uuid4()))
    source_role: AgentRole = AgentRole.CAPTURER
    target_role: AgentRole = AgentRole.CANONICALIZER
    user_id: str = ""
    timestamp: datetime = field(default_factory=lambda: datetime.now(timezone.utc))
    sub_scores_affected: list[str] = field(default_factory=list)
    trace_id: str = ""
    payload_type: str = ""          # "cloud_event", "aggregate", "domain_matrix", etc.
    payload: Any = None


# ─── The Communication Contract ───────────────────────────────
# Each role declares what it consumes and produces.

COMMUNICATION_GRAPH: dict[AgentRole, dict] = {
    AgentRole.CAPTURER: {
        "consumes": "sensor_api_response",
        "produces": "cloud_event",
        "next": AgentRole.CANONICALIZER,
        "matrix_cell": "S1-L1",
        "laws": ["I"],
    },
    AgentRole.CANONICALIZER: {
        "consumes": "cloud_event",
        "produces": "aggregate_row",
        "next": AgentRole.EMBEDDER,
        "matrix_cell": "S1-L1",
        "laws": ["I", "IV"],
        "invariants": ["I2"],
    },
    AgentRole.EMBEDDER: {
        "consumes": "aggregate_row",
        "produces": "domain_matrix",
        "next": AgentRole.COUPLER,
        "matrix_cell": "S1-L1",
        "laws": ["II"],
        "invariants": ["I4"],
    },
    AgentRole.COUPLER: {
        "consumes": "domain_matrix",
        "produces": "kappa_vector",
        "next": AgentRole.SYNTHESIZER,
        "matrix_cell": "S1-L1",
        "laws": ["II"],
        "invariants": ["I3"],
    },
    AgentRole.SYNTHESIZER: {
        "consumes": ["kappa_vector", "domain_matrix", "precision_vector"],
        "produces": "objective_result",
        "next": AgentRole.FEEDBACK,
        "matrix_cell": "S1-L1",
        "laws": ["II", "IV"],
        "invariants": ["I4"],
    },
    AgentRole.FEEDBACK: {
        "consumes": "objective_result",
        "produces": "closed_prediction",
        "next": [AgentRole.REFINER, AgentRole.AUDITOR],
        "matrix_cell": "S1-L1",
        "laws": ["III", "IV"],
        "invariants": ["I7"],
    },
    AgentRole.REFINER: {
        "consumes": ["closed_prediction", "meta_gradient"],
        "produces": ["model_version", "projection_update"],
        "next": AgentRole.EMBEDDER,  # Updated params flow back to embed
        "matrix_cell": "S1-L2 + S3-L2",
        "laws": ["IV", "V"],
        "invariants": ["I7"],
    },
    AgentRole.TUNER: {
        "consumes": ["model_version_history", "coupling_drift", "meta_gradient"],
        "produces": "precision_vector",
        "next": [AgentRole.SYNTHESIZER, AgentRole.REFINER],
        "matrix_cell": "S1-L3 + S3-L3",
        "laws": ["IV", "V"],
        "invariants": ["I8"],
    },
    AgentRole.AUDITOR: {
        "consumes": ["closed_prediction", "invariant_results"],
        "produces": "audit_result",
        "next": AgentRole.TUNER,  # Stagnation alerts trigger L3
        "matrix_cell": "cross-cutting",
        "laws": ["V"],
        "invariants": ["I1-I8"],
    },
}


def get_agent_contract(role: AgentRole) -> dict:
    """Return the communication contract for a given agent role."""
    return COMMUNICATION_GRAPH.get(role, {})


def validate_message_flow(source: AgentRole, target: AgentRole) -> bool:
    """Verify that this message flow is valid per the communication graph."""
    contract = COMMUNICATION_GRAPH.get(source, {})
    expected_next = contract.get("next")
    if isinstance(expected_next, list):
        return target in expected_next
    return target == expected_next
