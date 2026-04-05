"""
CSOS Core — PURE PHYSICS (ZERO I/O)

The 5 chemical equations of photosynthesis:
  Gouterman 1961: WHAT to absorb      (resonance bandwidth)
  Forster 1948:   HOW to transfer      (dipole coupling)
  Marcus 1956:    HOW to separate      (activation barrier)
  Mitchell 1961:  HOW to store         (proton gradient)
  Boyer 1997:     HOW to synthesize    (rotary motor)

All I/O handled by OpenCode tools. This file returns dicts only.
"""
from __future__ import annotations
import statistics, json
from dataclasses import dataclass, field
from pathlib import Path


# === PHOTON (signal event) ===

@dataclass
class Photon:
    cycle: int
    predicted: float
    actual: float
    error: float
    resonated: bool


# === ATOM (processing unit) ===

class Atom:
    __slots__ = ('name', 'formula', 'source', 'params', 'limits', 
                 'photons', 'local_photons', '_pending', 'born_in')

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

    @property
    def resonance_width(self):
        dof = len(self.params) + len(self.limits) + max(1, len(self.formula) // 10)
        return dof / (dof + 1)

    def predict(self, cycle, value):
        self._pending = (cycle, value)

    def observe(self, cycle, actual):
        if not self._pending:
            return None
        _, predicted = self._pending
        self._pending = None
        error = abs(predicted - actual) / max(abs(actual), abs(predicted) * .01 + 1e-10)
        resonated = error < self.resonance_width
        p = Photon(cycle, predicted, actual, error, resonated)
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

    def tune(self):
        if len(self.photons) < 3:
            return
        w = min(self.gradient or 1, len(self.photons))
        bias = statistics.mean(p.predicted - p.actual for p in self.photons[-w:])
        if abs(bias) < self.resonance_width * 0.1:
            return
        lr = 1 / (1 + self.gradient)
        for k in self.params:
            if isinstance(self.params[k], (int, float)):
                self.params[k] -= bias * lr * (abs(self.params[k]) or 0.01)

    def clone(self, from_ring):
        c = Atom(self.name, self.formula, f"{self.source} [{from_ring}]",
                 dict(self.params), list(self.limits))
        c.photons = list(self.photons)
        c.local_photons = []
        c.born_in = from_ring
        return c

    def to_dict(self):
        return {
            "name": self.name,
            "formula": self.formula,
            "source": self.source,
            "params": self.params,
            "limits": self.limits,
            "born_in": self.born_in,
            "photons": [{"c": p.cycle, "p": round(p.predicted, 4), "a": round(p.actual, 4),
                         "e": round(p.error, 4), "r": p.resonated} for p in self.photons]
        }

    @staticmethod
    def from_dict(d):
        a = Atom(d["name"], d.get("formula", ""), d.get("source", ""),
                 d.get("params", {}), d.get("limits", []))
        a.born_in = d.get("born_in", "")
        a.photons = [Photon(p["c"], p["p"], p["a"], p["e"], p["r"]) for p in d.get("photons", [])]
        return a


# === RING (organizational compartment) ===

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


# === ECOSYSTEM PRIORS (the 5 equations) ===

EQUATIONS = [
    {"name": "gouterman", "formula": "dE=hc/l", "source": "Gouterman 1961",
     "params": {"dE": 1, "h": 1, "c": 1, "l": 1}, "limits": ["l->inf->0"]},
    {"name": "forster", "formula": "k_ET=(1/t)(R0/r)^6", "source": "Forster 1948",
     "params": {"k": 1, "t": 1, "R0": 1, "r": 1}, "limits": ["r=R0->1/t"]},
    {"name": "marcus", "formula": "k=exp(-(dG+l)^2/4lkT)", "source": "Nobel 1992",
     "params": {"k": 1, "V": 1, "l": 1, "dG": 1}, "limits": ["dG=-l->max"]},
    {"name": "mitchell", "formula": "dG=-nFdy+2.3RT*dpH", "source": "Nobel 1978",
     "params": {"dG": 1, "n": 1, "F": 1, "dy": 1}, "limits": ["eq"]},
    {"name": "boyer", "formula": "ATP=flux*n/3", "source": "Nobel 1997",
     "params": {"rate": 1, "flux": 1, "n": 1}, "limits": ["flux=0->0"]},
]


# === CORE (PURE PHYSICS - RETURNS DICTS ONLY) ===

class Core:
    """Pure physics engine. NO file I/O. Returns dicts only."""

    def __init__(self, root=None):
        self._dir = Path(root or ".") / ".csos"
        self._dir.mkdir(parents=True, exist_ok=True)
        self._rings = {}
        self._load()

    def grow(self, name, priors=None, eco=False):
        atoms = []
        for p in (priors or EQUATIONS):
            a = Atom(p.get("name", ""), p.get("formula", ""), p.get("source", ""),
                     {v: 1.0 for v in p.get("params", {})}, p.get("limits", []))
            a.born_in = name
            atoms.append(a)

        ring = Ring(name=name, atoms=atoms)
        self._rings[name] = ring
        self._save(ring)
        return {"ring": name, "atoms": len(ring.atoms)}

    def fly(self, name, signals=None):
        ring = self._rings.get(name)
        if not ring:
            return {"error": "no ring '{}'".format(name)}

        sigs = signals or []
        produced = 0

        if sigs:
            for val in sigs:
                v = float(val)
                for atom in ring.atoms:
                    res = [p for p in atom.photons if p.resonated]
                    atom.predict(ring.cycles, res[-1].actual if res else v)
                    ph = atom.observe(ring.cycles, v)
                    if ph and ph.resonated:
                        produced += 1
            if len(ring.f_history) >= 2 and ring.f_history[-1] > ring.f_history[-2]:
                for atom in ring.atoms:
                    atom.tune()

        synthesized = self._calvin(ring)
        ring.f_history.append(ring.F)
        ring.cycles += 1
        ring.signals += len(sigs)
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
            "resonance_width": round(rw, 3)
        }

    def _calvin(self, ring):
        co2 = []
        for atom in ring.atoms:
            co2.extend(p for p in atom.local_photons if not p.resonated)
        if not co2:
            return 0
        actuals = [p.actual for p in co2[-50:]]
        if len(actuals) < max(1, int(len(co2) * 0.1)):
            return 0
        mean_co2 = statistics.mean(actuals)
        var_co2 = statistics.variance(actuals) if len(actuals) > 1 else 0
        if ring.local_gradient < max(0.1, statistics.mean([p.actual for p in co2]) * 0.05):
            return 0
        for ex in ring.atoms:
            if ex.local_photons:
                res = [p.actual for p in ex.local_photons if p.resonated]
                if res:
                    em = statistics.mean(res[-10:])
                    if abs(mean_co2 - em) / max(abs(em), 1e-10) < max(ex.resonance_width, var_co2 ** 0.5 * 0.1):
                        return 0
        new = Atom("calvin_c{}".format(ring.cycles),
                   "pattern@{:.1f}+/-{:.1f}".format(mean_co2, var_co2 ** 0.5),
                   "Calvin {}".format(ring.name), {"center": mean_co2}, [])
        new.born_in = ring.name
        ring.atoms.append(new)
        return 1

    def diffuse(self, src_name, tgt_name):
        src, tgt = self._rings.get(src_name), self._rings.get(tgt_name)
        if not src or not tgt:
            return {"error": "not found"}
        existing = {a.name for a in tgt.atoms}
        n = 0
        for atom in src.atoms:
            if atom.name in existing:
                continue
            g = atom.gradient
            r = 1.0 / (1 + g) if g > 0 else float("inf")
            if (1.0 / max(r, 1e-10)) ** 6 > 1.0:
                tgt.atoms.append(atom.clone(src_name))
                n += 1
        if n > 0: self._save(tgt)
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
                # Wisdom signals: measures flow, not pipe
                total_photons = sum(len(a.photons) for a in r.atoms)
                resonated_photons = sum(a.gradient for a in r.atoms)
                calvin_atoms = sum(1 for a in r.atoms if a.name.startswith("calvin_"))

                # Specificity delta: calvin atoms per cycle (proxy for convergence)
                specificity_delta = calvin_atoms / max(1, r.cycles)

                # Action ratio: resonated / total photons
                action_ratio = resonated_photons / max(1, total_photons)

                # Calvin discovery rate
                calvin_rate = calvin_atoms / max(1, len(r.atoms))

                # Boundary crossings: F crossing resonance_width
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
                    "gradient_gap": None,  # requires domain ring, computed externally
                    "calvin_rate": round(calvin_rate, 4),
                    "boundary_crossings": crossings,
                }

            else:
                return {
                    "name": r.name, "F": round(r.F, 4), "gradient": r.gradient,
                    "speed": round(r.speed, 4), "resonance_width": round(rw, 3),
                    "cycles": r.cycles, "atoms": len(r.atoms),
                    "atoms": [{"name": a.name, "gradient": a.gradient, "F": round(a.F, 4),
                               "resonance_width": round(a.resonance_width, 3)} for a in r.atoms]
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
        (d / f"{r.name}.json").write_text(json.dumps({"name":r.name,"f_history":r.f_history,
            "cycles":r.cycles,"signals":r.signals,
            "atoms":[a.to_dict() for a in r.atoms]},default=str))

    def _load(self):
        d = self._dir / "rings"
        if not d.exists(): return
        for f in d.glob("*.json"):
            try:
                data = json.loads(f.read_text())
                r = Ring(name=data["name"],atoms=[Atom.from_dict(a) for a in data.get("atoms",[])],
                         f_history=data.get("f_history",[]),cycles=data.get("cycles",0),
                         signals=data.get("signals",0))
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
            if not (0 <= ring.F <= 1):
                issues.append("F out of range: {}".format(ring.F))
            if ring.speed < 0:
                issues.append("Speed negative: {}".format(ring.speed))
            if ring.gradient < 0:
                issues.append("Gradient negative: {}".format(ring.gradient))

            return {
                "ring": name,
                "status": "pass" if not issues else "fail",
                "compliance_score": round(1 - len(issues) * 0.25, 3),
                "issues": issues,
                "checks": {
                    "atoms_present": len(ring.atoms) > 0,
                    "f_valid": 0 <= ring.F <= 1,
                    "speed_valid": ring.speed >= 0,
                    "gradient_valid": ring.gradient >= 0
                }
            }
        else:
            results = {name: self.lint(name) for name in self._rings.keys()}
            return {"rings_linted": len(results), "results": results}
