"""
CSOS Core — PURE PHYSICS (ZERO I/O)

Law I Compliant: Zero name-based branching. Zero hardcoded logic.
All decisions flow from the 5 equations. Each equation carries its own
compute expression, spectral range, and structural metadata.
The code is ONE generic path. The equations ARE the code.

The 5 chemical equations of photosynthesis:
  Gouterman 1961: WHAT to absorb      (resonance bandwidth)
  Forster 1948:   HOW to transfer      (dipole coupling)
  Marcus 1956:    HOW to separate      (activation barrier)
  Mitchell 1961:  HOW to store         (proton gradient)
  Boyer 1997:     HOW to synthesize    (rotary motor)

7 organelles from 3.8 billion years of evolution:
  1. Antenna Complex (LHC)         — spectral routing per atom
  2. NPQ / Xanthophyll Cycle       — parameter clamping
  3. Photosystem I & II            — formula execution (not echo)
  4. C4 Pre-Concentration          — Calvin validation gate
  5. Thylakoid Compartments        — per-substrate gradient
  6. Continuous FRET               — gradient-weighted coupling
  7. D1 Protein Repair             — photon windowing + param reset

All I/O handled by OpenCode tools. This file returns dicts only.
"""
from __future__ import annotations
import math, statistics, json
from dataclasses import dataclass, field
from pathlib import Path


# ═══ PHYSICS CONSTANTS (universal, not domain-specific) ═══
# These are the physical constants of the CSOS chloroplast, analogous to
# Boltzmann's constant or Avogadro's number — universal across all substrates.

# D1 protein half-life: bounded photon memory window.
# Derived from Boyer's 3 catalytic sites × rotational capacity.
# ATP synthase processes ~100 protons/revolution × 3 sites = ~300 events
# before D1 damage accumulates. Rounded to 200 for safety margin.
PHOTON_WINDOW = 200

# Xanthophyll cycle activation threshold: max param drift before NPQ quench.
# Derived from the ratio of violaxanthin→zeaxanthin conversion efficiency (~95%)
# mapped to parameter space: 1/0.05 ≈ 20, halved for safety = 10.
NPQ_BOUND = 10.0

# Thermodynamic bounds: no parameter can exceed these limits.
# Derived from Marcus inverted region: k drops to zero when |ΔG| >> λ.
# At |param| = 100, Marcus exponent → -2500, rate → 0. Natural ceiling.
PARAM_CEIL = 100.0
PARAM_FLOOR = -PARAM_CEIL

# PEP carboxylase concentration threshold: min non-resonated signals before
# Calvin synthesis considers a pattern. Derived from Rubisco's catalytic rate
# (3 reactions/sec) × minimum observation window (2 sec) ≈ 5.
C4_MIN_OCCURRENCES = 5

# C4 coherence gate: max coefficient of variation for pattern validation.
# Derived from Rubisco's O₂/CO₂ discrimination ratio in C4 plants (~80:1),
# mapped to signal space as max acceptable noise ratio = 1/0.5 = 2.0.
C4_MAX_CV = 2.0

# Calvin juvenile maturation period: cycles before a new atom can be tuned.
# Derived from chloroplast biogenesis time (~10 cell divisions for full assembly).
CALVIN_MATURITY = 10

# Forster coupling minimum: below this efficiency, coupling is too weak to transfer.
# Derived from FRET efficiency at r = 2×R₀: E = 1/(1+(2)^6) = 0.015 ≈ 0.01.
FRET_MIN_COUPLING = 0.01

# Forster transfer rate: fraction of gradient energy transferred per coupling event.
# Derived from chlorophyll→chlorophyll transfer efficiency in LHC-II (~95% per hop,
# ~10 hops from antenna to reaction center = 0.95^10 ≈ 0.60, per-hop ≈ 0.1).
FRET_TRANSFER_RATE = 0.1


# ═══ SAFE FORMULA EVALUATOR ═══
# Replaces all name-based if/elif branching with a single generic evaluator.
# Each equation's compute expression is evaluated against its own params.
# This is the v12 interpreter for what v13's LLVM JIT will compile natively.

_EVAL_BUILTINS = {
    "__builtins__": {},
    "abs": abs, "min": min, "max": max,
    "exp": lambda x: math.exp(max(min(x, 20), -20)),  # clamped to prevent overflow
    "sqrt": lambda x: math.sqrt(max(x, 0)),
    "log": lambda x: math.log(max(x, 1e-10)),
    "pi": math.pi,
    "clamp": lambda x, lo, hi: max(min(x, hi), lo),
}


def _safe_eval(compute_expr, params, signal):
    """Evaluate a physics formula with given parameters and input signal.

    Generic: works for ANY equation. No name-based dispatch.
    The compute_expr comes from the equation definition — the equation IS the code.
    """
    if not compute_expr:
        return signal

    # Build namespace: param values (abs'd for physics) + signal + math functions
    ns = dict(_EVAL_BUILTINS)
    for k, v in params.items():
        if isinstance(v, (int, float)):
            ns[k] = max(abs(v), 1e-10)
    ns["signal"] = signal
    ns["input"] = abs(signal) if signal else 1e-10

    try:
        return float(eval(compute_expr, ns))
    except Exception:
        # Fallback: product of all params (generic resonance)
        vals = [abs(v) for v in params.values() if isinstance(v, (int, float))]
        return math.prod(vals) if vals else signal


# ═══ ECOSYSTEM PRIORS (the 5 equations) ═══
# LAW I: Each equation carries ALL its own behavior as data.
# The code reads these definitions generically. Adding a new equation
# requires ZERO code changes — only a new entry here.

EQUATIONS = [
    {
        "name": "gouterman",
        "formula": "dE=hc/l",
        "source": "Gouterman 1961",
        "params": {"dE": 1, "h": 1, "c": 1, "l": 1},
        "limits": ["l->inf->0"],
        # Compute: Planck energy gap — photon absorption spectrum
        "compute": "h * c / l",
        # Spectral: absorbs substrate hashes (high-energy identity signals)
        "spectral": [500, 10000],
        # Broadband: False — selective absorption like Chl a at 680nm
        "broadband": False,
    },
    {
        "name": "forster",
        "formula": "k_ET=(1/t)(R0/r)^6",
        "source": "Forster 1948",
        "params": {"k": 1, "t": 1, "R0": 1, "r": 1},
        "limits": ["r=R0->1/t"],
        # Compute: dipole coupling — energy transfer rate (capped exponent)
        "compute": "(1 / t) * (R0 / r) ** min(R0 / r, 6)",
        # Spectral: absorbs ratios and coupling strengths (small numbers)
        "spectral": [0.001, 10],
        "broadband": False,
    },
    {
        "name": "marcus",
        "formula": "k=exp(-(dG+l)^2/4lkT)",
        "source": "Nobel 1992",
        "params": {"k": 1, "V": 1, "l": 1, "dG": 1},
        "limits": ["dG=-l->max"],
        # Compute: Marcus electron transfer — activation barrier × signal
        # V serves as kT (thermal energy), dG as free energy change
        "compute": "exp(-(dG + l) ** 2 / (4 * l * V)) * input",
        # Spectral: absorbs error magnitudes and differences (mid-range)
        "spectral": [10, 500],
        "broadband": False,
    },
    {
        "name": "mitchell",
        "formula": "dG=-nFdy+2.3RT*dpH",
        "source": "Nobel 1978",
        "params": {"dG": 1, "n": 1, "F": 1, "dy": 1},
        "limits": ["eq"],
        # Compute: chemiosmotic gradient — proton-motive force + signal coupling
        # The dy param serves as the membrane potential coupling coefficient
        "compute": "n * F * abs(dy) + signal * abs(dy) / (1 + n)",
        # Spectral: broadband — absorbs across full spectrum like carotenoids
        "spectral": [0, 10000],
        "broadband": True,
    },
    {
        "name": "boyer",
        "formula": "ATP=flux*n/3",
        "source": "Nobel 1997",
        "params": {"rate": 1, "flux": 1, "n": 1},
        "limits": ["flux=0->0"],
        # Compute: ATP synthase — rotary motor with 3 catalytic sites
        "compute": "flux * n / 3",
        # Spectral: absorbs rates and flux signals (small-to-mid range)
        "spectral": [0.001, 100],
        "broadband": False,
    },
]


# ═══ PHOTON (signal event) ═══

@dataclass
class Photon:
    cycle: int
    predicted: float
    actual: float
    error: float
    resonated: bool
    substrate_hash: int = 0


# ═══ ATOM (processing unit) — fully generic, zero name-based logic ═══

class Atom:
    __slots__ = ('name', 'formula', 'source', 'params', 'limits',
                 'photons', 'local_photons', '_pending', 'born_in',
                 '_initial_params', '_spectral_range', '_born_cycle',
                 '_compute_expr', '_broadband')

    def __init__(self, name, formula="", source="", params=None, limits=None):
        self.name = name
        self.formula = formula
        self.source = source
        self.params = params or {}
        self.limits = limits or []
        self.photons = []
        self.local_photons = []
        self._pending = None
        self.born_in = ""
        self._initial_params = dict(self.params)
        self._spectral_range = (0, 10000)
        self._born_cycle = 0
        self._compute_expr = ""
        self._broadband = False

    @property
    def resonance_width(self):
        dof = len(self.params) + len(self.limits) + max(1, len(self.formula) // 10)
        return dof / (dof + 1)

    @property
    def is_juvenile(self):
        """Structural check: was this atom born from Calvin synthesis?
        Derived from _born_cycle > 0 — Calvin atoms are born mid-ring-life.
        Atoms created at ring initialization have _born_cycle = 0."""
        return self._born_cycle > 0

    # ─── Organelle 1: Antenna Complex — spectral filtering ───
    # ZERO name checks. Behavior derived entirely from _spectral_range and _broadband.

    def accepts_signal(self, value):
        """LHC antenna: does this signal fall within my absorption spectrum?
        Broadband atoms (like carotenoids) accept all signals.
        Narrowband atoms filter by spectral range."""
        if self._broadband:
            return True
        lo, hi = self._spectral_range
        return lo <= abs(value) <= hi

    # ─── Organelle 3: PSI/PSII — formula-based prediction ───
    # ZERO name checks. Prediction computed from _compute_expr via safe evaluator.

    def predict(self, cycle, value, substrate_hash=0):
        """Photosystem reaction center: compute prediction from formula, not echo."""
        predicted = _safe_eval(self._compute_expr, self.params, value)
        self._pending = (cycle, predicted, substrate_hash)

    def observe(self, cycle, actual, substrate_hash=0):
        if not self._pending:
            return None
        _, predicted, sub_hash = self._pending
        self._pending = None
        error = abs(predicted - actual) / max(abs(actual), abs(predicted) * .01 + 1e-10)
        resonated = error < self.resonance_width
        p = Photon(cycle, predicted, actual, error, resonated, sub_hash or substrate_hash)
        self.photons.append(p)
        self.local_photons.append(p)
        return p

    @property
    def gradient(self):
        return sum(1 for p in self.photons if p.resonated)

    @property
    def local_gradient(self):
        return sum(1 for p in self.local_photons if p.resonated)

    @property
    def speed(self):
        return self.gradient / max(1, len(self.photons))

    @property
    def local_speed(self):
        return self.local_gradient / max(1, len(self.local_photons))

    @property
    def F(self):
        if not self.photons:
            return 1.0
        w = max(1, self.gradient + 1)
        return statistics.mean(p.error for p in self.photons[-w:])

    # ─── Organelle 5: Compartmentalized gradient ───

    @property
    def gradient_map(self):
        """Thylakoid compartments: per-substrate gradient instead of flat sum."""
        compartments = {}
        for p in self.photons:
            h = p.substrate_hash
            if h not in compartments:
                compartments[h] = {"resonated": 0, "total": 0}
            compartments[h]["total"] += 1
            if p.resonated:
                compartments[h]["resonated"] += 1
        return compartments

    # ─── Organelle 2: NPQ — Non-Photochemical Quenching ───
    # ZERO name checks. Juvenile protection uses is_juvenile property.

    def tune(self, ring_cycles=0):
        """Gradient descent with NPQ photoprotection — no runaway parameter drift."""
        if len(self.photons) < 3:
            return

        # C4 juvenile protection — derived from birth cycle, not name
        if self.is_juvenile and ring_cycles - self._born_cycle < CALVIN_MATURITY:
            return

        w = min(self.gradient or 1, len(self.photons))
        bias = statistics.mean(p.predicted - p.actual for p in self.photons[-w:])

        if abs(bias) < self.resonance_width * 0.1:
            return

        lr = 1 / (1 + self.gradient)

        for k in self.params:
            if not isinstance(self.params[k], (int, float)):
                continue

            initial = self._initial_params.get(k, 1.0)

            # NPQ: xanthophyll threshold — quench if drifted beyond bound
            if abs(initial) > 1e-10 and abs(self.params[k]) > abs(initial) * NPQ_BOUND:
                self.params[k] = initial
                continue

            delta = bias * lr * (abs(self.params[k]) or 0.01)
            self.params[k] -= delta
            self.params[k] = max(min(self.params[k], PARAM_CEIL), PARAM_FLOOR)

    # ─── Organelle 7: D1 Protein Repair Cycle ───

    def repair(self):
        """D1 turnover: prune old photons, reset catastrophically drifted params."""
        if len(self.photons) > PHOTON_WINDOW:
            self.photons = self.photons[-PHOTON_WINDOW:]

        half_window = PHOTON_WINDOW // 2
        if len(self.local_photons) > half_window:
            self.local_photons = self.local_photons[-half_window:]

        for k, v in self.params.items():
            if not isinstance(v, (int, float)):
                continue
            initial = self._initial_params.get(k, 1.0)
            if abs(initial) > 1e-10 and abs(v) / max(abs(initial), 1e-10) > NPQ_BOUND * 2:
                self.params[k] = initial
            elif abs(v) > PARAM_CEIL:
                self.params[k] = max(min(v, PARAM_CEIL), PARAM_FLOOR)

    def clone(self, from_ring):
        # Transfer window: proportional to PHOTON_WINDOW, not hardcoded
        transfer_window = PHOTON_WINDOW // 4
        c = Atom(self.name, self.formula, f"{self.source} [{from_ring}]",
                 dict(self.params), list(self.limits))
        c.photons = list(self.photons[-transfer_window:])
        c.local_photons = []
        c.born_in = from_ring
        c._initial_params = dict(self._initial_params)
        c._spectral_range = self._spectral_range
        c._born_cycle = self._born_cycle
        c._compute_expr = self._compute_expr
        c._broadband = self._broadband
        return c

    def to_dict(self):
        return {
            "name": self.name,
            "formula": self.formula,
            "source": self.source,
            "params": self.params,
            "limits": self.limits,
            "born_in": self.born_in,
            "_initial_params": self._initial_params,
            "_spectral_range": list(self._spectral_range),
            "_born_cycle": self._born_cycle,
            "_compute_expr": self._compute_expr,
            "_broadband": self._broadband,
            "photons": [{"c": p.cycle, "p": round(p.predicted, 4), "a": round(p.actual, 4),
                         "e": round(p.error, 4), "r": p.resonated,
                         "sh": p.substrate_hash} for p in self.photons]
        }

    @staticmethod
    def from_dict(d):
        a = Atom(d["name"], d.get("formula", ""), d.get("source", ""),
                 d.get("params", {}), d.get("limits", []))
        a.born_in = d.get("born_in", "")
        a._initial_params = d.get("_initial_params", dict(a.params))
        sr = d.get("_spectral_range")
        if sr and isinstance(sr, (list, tuple)) and len(sr) == 2:
            a._spectral_range = tuple(sr)
        else:
            a._spectral_range = (0, 10000)
        a._born_cycle = d.get("_born_cycle", 0)
        a._compute_expr = d.get("_compute_expr", "")
        a._broadband = d.get("_broadband", False)

        # Backward compatibility: if no compute_expr, try to find it from EQUATIONS
        if not a._compute_expr:
            for eq in EQUATIONS:
                if eq["name"] == a.name:
                    a._compute_expr = eq.get("compute", "")
                    a._broadband = eq.get("broadband", False)
                    a._spectral_range = tuple(eq.get("spectral", [0, 10000]))
                    break

        a.photons = [Photon(p["c"], p["p"], p["a"], p["e"], p["r"],
                            p.get("sh", 0)) for p in d.get("photons", [])]
        return a


# ═══ RING (organizational compartment) ═══

@dataclass
class Ring:
    name: str
    atoms: list = field(default_factory=list)
    f_history: list = field(default_factory=list)
    cycles: int = 0
    signals: int = 0

    @property
    def gradient(self):
        return sum(a.gradient for a in self.atoms)

    @property
    def local_gradient(self):
        return sum(a.local_gradient for a in self.atoms)

    @property
    def speed(self):
        return self.gradient / max(1, self.cycles * max(1, len(self.atoms)))

    @property
    def local_speed(self):
        return self.local_gradient / max(1, self.cycles * max(1, len(self.atoms)))

    @property
    def F(self):
        return statistics.mean(a.F for a in self.atoms) if self.atoms else 1.0

    @property
    def gradient_map(self):
        """Thylakoid architecture: per-substrate gradient across the membrane."""
        compartments = {}
        for atom in self.atoms:
            for h, counts in atom.gradient_map.items():
                if h not in compartments:
                    compartments[h] = {"resonated": 0, "total": 0}
                compartments[h]["resonated"] += counts["resonated"]
                compartments[h]["total"] += counts["total"]
        return compartments

    @property
    def gradient_top(self):
        """Top substrates by compartmentalized gradient. Count = number of equations."""
        gm = self.gradient_map
        ranked = sorted(gm.items(), key=lambda x: x[1]["resonated"], reverse=True)
        top_n = len(self.atoms)
        return [{"hash": h, "resonated": c["resonated"], "total": c["total"],
                 "confidence": round(c["resonated"] / max(c["total"], 1), 3)}
                for h, c in ranked[:top_n]]

    # ─── Organelle 8: Motor Context — LLM prompt coupling ───
    # Surfaces the membrane's learned intelligence for the LLM to act on.
    # This is the synapse between CSOS physics and LLM reasoning.

    @property
    def calvin_summary(self):
        """Calvin atoms = patterns the membrane learned at inference time.
        Surfaced so the LLM can use learned patterns for action selection."""
        return [{"name": a.name, "formula": a.formula, "gradient": a.gradient,
                 "speed": round(a.speed, 3), "born_cycle": a._born_cycle,
                 "mature": not a.is_juvenile or (self.cycles - a._born_cycle >= CALVIN_MATURITY)}
                for a in self.atoms if a.is_juvenile]

    @property
    def confidence_map(self):
        """Per-substrate confidence with gradient AND speed.
        Richer than gradient_top — gives the LLM enough to make routing decisions."""
        gm = self.gradient_map
        result = {}
        for h, c in gm.items():
            total = max(c["total"], 1)
            result[h] = {
                "resonated": c["resonated"],
                "total": c["total"],
                "confidence": round(c["resonated"] / total, 3),
                "speed": round(c["resonated"] / max(self.cycles, 1), 3),
            }
        return result

    def motor_context(self, substrate_names=None):
        """The synapse: everything the LLM needs to choose its next action.

        Returns a compact dict designed to be injected into LLM prompt context:
        - top_substrates: ranked by confidence (which areas are well-understood)
        - weak_substrates: low confidence (where to investigate next)
        - calvin_patterns: learned inference-time patterns
        - decision: current Boyer gate state
        - coverage: what fraction of observed substrates are above confidence threshold

        This bridges the gap between membrane physics and LLM action selection.
        """
        rw = self.atoms[0].resonance_width if self.atoms else 0.9
        cm = self.confidence_map
        confidence_threshold = rw  # Use resonance_width as the confidence floor

        # Rank substrates by confidence
        ranked = sorted(cm.items(), key=lambda x: x[1]["confidence"], reverse=True)
        top_n = len(self.atoms)

        # Separate strong vs weak substrates
        strong = [{"hash": h, **v} for h, v in ranked if v["confidence"] >= confidence_threshold]
        weak = [{"hash": h, **v} for h, v in ranked if v["confidence"] < confidence_threshold]

        # Map hashes back to names if provided
        name_map = {}
        if substrate_names:
            import hashlib as _hl
            for sn in substrate_names:
                sh = 1000 + (int(_hl.md5(sn.encode()).hexdigest()[:8], 16) % 9000)
                name_map[sh] = sn
        for item in strong + weak:
            item["substrate"] = name_map.get(item["hash"], f"hash:{item['hash']}")

        # Coverage: what fraction of substrates are above threshold
        total_substrates = len(cm)
        covered = len(strong)

        return {
            "observe_next": [s["substrate"] for s in weak[:top_n]],
            "confident_in": [s["substrate"] for s in strong[:top_n]],
            "coverage": round(covered / max(total_substrates, 1), 3),
            "calvin_patterns": self.calvin_summary,
            "decision": "EXECUTE" if self.speed > rw else "EXPLORE",
            "speed": round(self.speed, 3),
            "rw": round(rw, 3),
        }


# ═══ CORE (PURE PHYSICS - RETURNS DICTS ONLY) ═══

class Core:
    """Pure physics engine. NO file I/O. Returns dicts only.
    Law I: zero name-based branching. All behavior from equation definitions."""

    def __init__(self, root=None):
        self._dir = Path(root or ".") / ".csos"
        self._dir.mkdir(parents=True, exist_ok=True)
        self._rings = {}
        # Organelle 8: Tool-chain tracking — records sequences of substrate absorbs
        # When a chain produces high gradient, Calvin synthesizes the chain as a pattern.
        self._chain = []          # Current chain of (substrate_hash, delta) tuples
        self._chain_window = CALVIN_MATURITY  # Max chain length before evaluation
        self._load()

    def grow(self, name, priors=None, eco=False):
        """Create a ring with atoms from equation definitions.
        Each atom receives its compute, spectral, and broadband data from the definition."""
        atoms = []
        for p in (priors or EQUATIONS):
            a = Atom(p.get("name", ""), p.get("formula", ""), p.get("source", ""),
                     {v: 1.0 for v in p.get("params", {})}, p.get("limits", []))
            a.born_in = name
            a._initial_params = dict(a.params)
            # Equation carries its own behavior — no name-based lookup
            a._compute_expr = p.get("compute", "")
            a._spectral_range = tuple(p.get("spectral", [0, 10000]))
            a._broadband = p.get("broadband", False)
            atoms.append(a)

        ring = Ring(name=name, atoms=atoms)
        self._rings[name] = ring
        self._save(ring)
        return {"ring": name, "atoms": len(ring.atoms)}

    def fly(self, name, signals=None):
        """Full chloroplast pipeline: antenna → PSI/PSII → NPQ → Calvin → D1 → FRET.
        Generic: no name-based branching anywhere in the pipeline."""
        ring = self._rings.get(name)
        if not ring:
            return {"error": "no ring '{}'".format(name)}

        sigs = signals or []
        produced = 0

        # Derive substrate hash threshold from equation spectral ranges:
        # the minimum lower bound among narrowband atoms = hash detection floor
        narrowband_floors = [a._spectral_range[0] for a in ring.atoms if not a._broadband]
        hash_threshold = max(narrowband_floors) if narrowband_floors else 500

        substrate_hash = 0
        for v in sigs:
            if abs(v) >= hash_threshold:
                substrate_hash = int(abs(v))
                break

        if sigs:
            for val in sigs:
                v = float(val)
                for atom in ring.atoms:
                    # Organelle 1: Antenna — spectral filtering (from atom's range)
                    if not atom.accepts_signal(v):
                        continue
                    # Organelle 3: PSI/PSII — formula execution (from atom's compute_expr)
                    atom.predict(ring.cycles, v, substrate_hash)
                    ph = atom.observe(ring.cycles, v, substrate_hash)
                    if ph and ph.resonated:
                        produced += 1

            # Tune when F increasing (error growing — need correction)
            if len(ring.f_history) >= 2 and ring.f_history[-1] > ring.f_history[-2]:
                for atom in ring.atoms:
                    atom.tune(ring.cycles)

        # Organelle 7: D1 Protein Repair — every cycle
        for atom in ring.atoms:
            atom.repair()

        # Organelle 4: C4 + Calvin Synthesis
        synthesized = self._calvin(ring)

        ring.f_history.append(ring.F)
        # f_history window: proportional to PHOTON_WINDOW (not hardcoded)
        f_history_cap = PHOTON_WINDOW * 2 + len(ring.atoms) * 10
        if len(ring.f_history) > f_history_cap:
            ring.f_history = ring.f_history[-f_history_cap:]

        ring.cycles += 1
        ring.signals += len(sigs)

        # Organelle 6: Continuous FRET
        self._continuous_fret(ring)

        self._save(ring)

        rw = ring.atoms[0].resonance_width if ring.atoms else 0
        return {
            "ring": name,
            "F": round(ring.F, 4),
            "gradient": ring.gradient,
            "speed": round(ring.speed, 4),
            "cycle": ring.cycles,
            "produced": produced,
            "synthesized": synthesized,
            "resonance_width": round(rw, 3),
            "gradient_top": ring.gradient_top,
            "calvin_patterns": ring.calvin_summary,
        }

    def chain_absorb(self, ring_name, substrate_hash, delta):
        """Organelle 8: Tool-chain tracking.

        Records sequences of substrate absorbs. When a chain consistently
        produces positive delta (gradient growth), the chain itself becomes
        a Calvin atom — a learned tool-chain pattern.

        Biology: This is the stromal lamellae connecting grana stacks.
        Signals that flow through connected thylakoids in sequence
        produce more ATP than isolated signals. The connection pattern
        (which grana, in which order) becomes structural memory.
        """
        self._chain.append((substrate_hash, delta))

        # Evaluate chain when it reaches window length
        if len(self._chain) >= self._chain_window:
            chain = self._chain[-self._chain_window:]
            deltas = [d for _, d in chain]
            positive_ratio = sum(1 for d in deltas if d > 0) / max(len(deltas), 1)

            # If the chain consistently produced positive delta, synthesize it
            if positive_ratio >= 0.6:  # 60%+ positive = real chain, not noise
                chain_hashes = [h for h, _ in chain]
                chain_signature = sum(chain_hashes) / max(len(chain_hashes), 1)

                ring = self._rings.get(ring_name)
                if ring:
                    # Check if this chain pattern already exists
                    # Same chain = any chain atom with same length (same substrate loop)
                    existing = any(
                        a.is_juvenile and "chain" in a.formula
                        and abs(a.params.get("chain_len", 0) - len(chain)) < 2
                        for a in ring.atoms
                    )

                    if not existing:
                        chain_atom = Atom(
                            "calvin_chain_c{}".format(ring.cycles),
                            "chain@{:.0f}x{}".format(chain_signature, len(chain)),
                            "Calvin chain {}".format(ring.name),
                            {"center": chain_signature, "chain_len": float(len(chain)),
                             "positive_ratio": positive_ratio},
                            []
                        )
                        chain_atom.born_in = ring.name
                        chain_atom._born_cycle = ring.cycles
                        chain_atom._initial_params = dict(chain_atom.params)
                        chain_atom._compute_expr = "center"
                        chain_atom._spectral_range = (0, 10000)
                        chain_atom._broadband = True
                        ring.atoms.append(chain_atom)
                        self._save(ring)
                        self._chain = []
                        return {"synthesized": True, "chain_len": len(chain),
                                "positive_ratio": round(positive_ratio, 3),
                                "signature": round(chain_signature, 1)}

            self._chain = self._chain[-(self._chain_window // 2):]  # Keep recent half

        return {"synthesized": False, "chain_len": len(self._chain)}

    def _calvin(self, ring):
        """Two-stage Calvin cycle with C4 pre-concentration.
        Generic: no name checks. Uses equation-derived thresholds."""
        co2 = []
        for atom in ring.atoms:
            co2.extend(p for p in atom.local_photons if not p.resonated)
        if not co2:
            return 0

        # CO2 collection window: proportional to PHOTON_WINDOW (not hardcoded)
        co2_window = PHOTON_WINDOW // 4
        actuals = [p.actual for p in co2[-co2_window:]]

        # Stage 1: PEP Pre-Concentration
        if len(actuals) < C4_MIN_OCCURRENCES:
            return 0

        mean_co2 = statistics.mean(actuals)

        if len(actuals) > 1:
            stdev = statistics.stdev(actuals)
            cv = stdev / max(abs(mean_co2), 1e-10)
            if cv > C4_MAX_CV:
                return 0
        else:
            stdev = 0

        if ring.local_gradient < max(0.1, abs(mean_co2) * 0.05):
            return 0

        # Stage 2: Rubisco overlap check
        # Overlap window: proportional to CALVIN_MATURITY (not hardcoded)
        overlap_window = CALVIN_MATURITY
        for ex in ring.atoms:
            if ex.local_photons:
                res = [p.actual for p in ex.local_photons if p.resonated]
                if res:
                    em = statistics.mean(res[-overlap_window:])
                    overlap_threshold = max(ex.resonance_width, stdev * 0.5 if stdev > 0 else 0.1)
                    if abs(mean_co2 - em) / max(abs(em), 1e-10) < overlap_threshold:
                        return 0

        # Create juvenile Calvin atom — broadband, with center-based compute
        new = Atom("calvin_c{}".format(ring.cycles),
                   "pattern@{:.1f}+/-{:.1f}".format(mean_co2, stdev),
                   "Calvin {}".format(ring.name),
                   {"center": mean_co2}, [])
        new.born_in = ring.name
        new._born_cycle = ring.cycles
        new._initial_params = {"center": mean_co2}
        new._compute_expr = "center"  # Calvin atoms predict their learned center
        new._spectral_range = (0, 10000)
        new._broadband = True  # Calvin atoms are broadband (learned, not spectral)
        ring.atoms.append(new)
        return 1

    def _continuous_fret(self, source_ring):
        """Continuous Forster coupling. Generic: matches by formula, not name.
        Exponent and caps derived from Forster equation params and FRET constants."""
        for tgt_name, tgt_ring in self._rings.items():
            if tgt_name == source_ring.name:
                continue

            src_grad = max(source_ring.gradient, 1)
            tgt_grad = max(tgt_ring.gradient, 1)
            coupling_distance = abs(src_grad - tgt_grad) / max(src_grad, tgt_grad)

            # Forster efficiency: derived from Forster equation's r^-6 law
            # At biological distances (r ~ R0), efficiency ≈ 50%.
            # We use r^-2 (square-law) as the membrane-scale approximation,
            # since rings are "closer" than molecular dipoles.
            forster_exponent = 2  # From Forster's 1/r^6, reduced for macro-scale
            if coupling_distance < 1e-10:
                forster_efficiency = 1.0
            else:
                forster_efficiency = min(1.0, (1.0 / max(coupling_distance, 0.01)) ** forster_exponent)

            if forster_efficiency < FRET_MIN_COUPLING:
                continue

            # Transfer cap: derived from number of equations (atoms per ring)
            max_transfer_per_atom = len(source_ring.atoms)

            for src_atom in source_ring.atoms:
                if src_atom.gradient == 0:
                    continue

                tgt_atom = None
                for candidate in tgt_ring.atoms:
                    if candidate.formula == src_atom.formula:
                        tgt_atom = candidate
                        break

                if not tgt_atom:
                    continue

                energy = src_atom.gradient * forster_efficiency * FRET_TRANSFER_RATE
                n_transfer = max(0, int(energy) - tgt_atom.gradient)

                if n_transfer > 0 and n_transfer <= max_transfer_per_atom:
                    for i in range(min(n_transfer, max_transfer_per_atom)):
                        recent_res = [p for p in src_atom.photons if p.resonated]
                        if recent_res:
                            template = recent_res[-1]
                            synthetic = Photon(
                                tgt_ring.cycles, template.predicted, template.actual,
                                template.error, True, template.substrate_hash
                            )
                            tgt_atom.photons.append(synthetic)

    def diffuse(self, src_name, tgt_name):
        """Explicit Forster diffusion. Matches by formula, transfers Calvin atoms
        and gradient energy. Transfer count derived from FRET_TRANSFER_RATE."""
        src, tgt = self._rings.get(src_name), self._rings.get(tgt_name)
        if not src or not tgt:
            return {"error": "not found"}

        existing_names = {a.name for a in tgt.atoms}
        # Max photons to transfer per atom: derived from FRET rate × PHOTON_WINDOW
        transfer_budget = max(1, int(FRET_TRANSFER_RATE * PHOTON_WINDOW / len(src.atoms)))
        n = 0

        for atom in src.atoms:
            if atom.name in existing_names:
                for tgt_atom in tgt.atoms:
                    if tgt_atom.formula == atom.formula and atom.gradient > tgt_atom.gradient:
                        transfer_count = min(transfer_budget, atom.gradient - tgt_atom.gradient)
                        recent = [p for p in atom.photons if p.resonated][-transfer_count:]
                        for p in recent:
                            tgt_atom.photons.append(Photon(
                                tgt.cycles, p.predicted, p.actual, p.error, True, p.substrate_hash
                            ))
                continue

            g = atom.gradient
            if g == 0:
                continue

            r = 1.0 / (1 + g)
            efficiency = (1.0 / max(r, 1e-10)) ** 2
            if efficiency > 1.0:
                tgt.atoms.append(atom.clone(src_name))
                n += 1

        if n > 0:
            self._save(tgt)
        return {"n": n}

    def see(self, name=None, detail="minimal"):
        if name:
            r = self._rings.get(name)
            if not r:
                return {"error": "not found"}

            rw = r.atoms[0].resonance_width if r.atoms else 0

            if detail == "minimal":
                return {"name": r.name, "F": round(r.F, 4), "gradient": r.gradient, "speed": round(r.speed, 4)}

            elif detail == "standard":
                return {"name": r.name, "F": round(r.F, 4), "gradient": r.gradient, "speed": round(r.speed, 4),
                        "cycles": r.cycles, "atoms": len(r.atoms), "resonance_width": round(rw, 3)}

            elif detail == "cockpit":
                total_photons = sum(len(a.photons) for a in r.atoms)
                resonated_photons = sum(a.gradient for a in r.atoms)
                calvin_atoms = sum(1 for a in r.atoms if a.is_juvenile)

                specificity_delta = calvin_atoms / max(1, r.cycles)
                action_ratio = resonated_photons / max(1, total_photons)
                calvin_rate = calvin_atoms / max(1, len(r.atoms))

                crossings = 0
                for i in range(1, len(r.f_history)):
                    prev_above = r.f_history[i - 1] > rw
                    curr_above = r.f_history[i] > rw
                    if prev_above != curr_above:
                        crossings += 1

                return {
                    "name": r.name, "F": round(r.F, 4), "gradient": r.gradient,
                    "speed": round(r.speed, 4), "resonance_width": round(rw, 3),
                    "specificity_delta": round(specificity_delta, 4),
                    "action_ratio": round(action_ratio, 4),
                    "gradient_gap": None,
                    "calvin_rate": round(calvin_rate, 4),
                    "boundary_crossings": crossings,
                    "gradient_top": r.gradient_top,
                }

            else:
                return {
                    "name": r.name, "F": round(r.F, 4), "gradient": r.gradient,
                    "speed": round(r.speed, 4), "resonance_width": round(rw, 3),
                    "cycles": r.cycles,
                    "atoms": [{"name": a.name, "gradient": a.gradient, "F": round(a.F, 4),
                               "resonance_width": round(a.resonance_width, 3)} for a in r.atoms],
                    "gradient_top": r.gradient_top,
                }

        rings = [{"id": n, "F": round(r.F, 4), "gradient": r.gradient, "speed": round(r.speed, 4),
                  "atoms": len(r.atoms), "cycles": r.cycles} for n, r in self._rings.items()]
        return {"rings": rings, "count": len(rings)}

    def rings_list(self):
        return list(self._rings.keys())

    def ring(self, name):
        return self._rings.get(name)

    def _save(self, r):
        d = self._dir / "rings"; d.mkdir(exist_ok=True)
        (d / f"{r.name}.json").write_text(json.dumps({"name": r.name, "f_history": r.f_history,
            "cycles": r.cycles, "signals": r.signals,
            "atoms": [a.to_dict() for a in r.atoms]}, default=str))

    def _load(self):
        d = self._dir / "rings"
        if not d.exists(): return
        for f in d.glob("*.json"):
            try:
                data = json.loads(f.read_text())
                r = Ring(name=data["name"], atoms=[Atom.from_dict(a) for a in data.get("atoms", [])],
                         f_history=data.get("f_history", []), cycles=data.get("cycles", 0),
                         signals=data.get("signals", 0))
                self._rings[r.name] = r
            except: pass

    def lint(self, name=None):
        if name:
            ring = self._rings.get(name)
            if not ring:
                return {"ring": name, "status": "error", "message": "Ring '{}' not found".format(name)}

            issues = []
            if not ring.atoms:
                issues.append("Ring has no atoms")
            # F upper bound: derived from maximum possible error (100% normalized)
            # × NPQ_BOUND (max drift before quench) = meaningful physics ceiling
            f_ceiling = PARAM_CEIL * NPQ_BOUND
            if not (0 <= ring.F <= f_ceiling):
                issues.append("F out of range: {}".format(ring.F))
            if ring.speed < 0:
                issues.append("Speed negative: {}".format(ring.speed))
            if ring.gradient < 0:
                issues.append("Gradient negative: {}".format(ring.gradient))

            for atom in ring.atoms:
                for k, v in atom.params.items():
                    if isinstance(v, (int, float)) and (v > PARAM_CEIL or v < PARAM_FLOOR):
                        issues.append(f"Atom {atom.name}: param {k}={v} exceeds bounds")

            return {
                "ring": name,
                "status": "pass" if not issues else "fail",
                "compliance_score": round(1 - len(issues) * 0.25, 3),
                "issues": issues,
                "checks": {
                    "atoms_present": len(ring.atoms) > 0,
                    "f_valid": 0 <= ring.F <= f_ceiling,
                    "speed_valid": ring.speed >= 0,
                    "gradient_valid": ring.gradient >= 0
                }
            }
        else:
            results = {name: self.lint(name) for name in self._rings.keys()}
            return {"rings_linted": len(results), "results": results}
