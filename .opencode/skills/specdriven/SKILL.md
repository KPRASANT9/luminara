# Spec-Driven Development

> The spec IS the code. `specs/eco.csos` defines the 5 equations. `spec_parse.c` loads them at runtime. `formula_jit.c` compiles them to native SIMD. Adding an equation = adding an atom block. Zero C changes.

## The .csos Format

```
atom <name> {
    formula:   <display equation>;
    compute:   <JIT-compilable expression>;
    source:    "<citation>";
    params:    { <key>: <default>, ... };
    limits:    [ "<constraint>" ];
    spectral:  [<lo>, <hi>];
    broadband: true|false;
    role:      "<what question does this answer>";
}

ring <name> {
    atoms: [<atom1>, <atom2>, ...];
    purpose: "<what signals this ring absorbs>";
}

agent <name> {
    mode: read_only | read_write | full;
    loop { ... }
}

law <name> {
    enforcement: "<what is enforced>";
    enforced_by: ["<mechanism>"];
}
```

## The 5 Equations (from specs/eco.csos)

| Atom | Formula | Compute (JIT'd) | Spectral | Role |
|------|---------|-----------------|----------|------|
| **gouterman** | dE = hc/lambda | `h * c / l` | [500, 10000] | Signal resonance matching |
| **forster** | k = (1/t)(R0/r)^6 | `(1/t)*(R0/r)**min(R0/r,6)` | [0.001, 10] | Substrate coupling |
| **marcus** | k = exp(-(dG+l)^2/4lkT) | `exp(-(dG+l)**2/(4*l*V))*input` | [10, 500] | Error correction |
| **mitchell** | dG = -nFdy+2.3RT*dpH | `n*F*abs(dy)+signal*abs(dy)/(1+n)` | [0, 10000] | Evidence accumulation |
| **boyer** | ATP = flux*n/3 | `flux * n / 3` | [0.001, 100] | Decision gate |

### How Equations Become Native Code

```
specs/eco.csos          "compute: h * c / l"
       │
   spec_parse.c         Parse at binary startup
       │
   formula_jit.c        build_formula_fn() → LLVM IR
       │
   LLVM ORC JIT         "default<O2>" optimization
       │
   Native SIMD          csos_formula_jit_eval(atom_index, params, signal)
```

## Creating a New Spec

### Example: Market Price Tracking

```
atom price_momentum {
    formula:   momentum = delta_price / time_window;
    compute:   signal / max(input, 0.001);
    source:    "Technical Analysis";
    params:    { signal: 1.0, input: 1.0 };
    spectral:  [0.1, 1000];
    broadband: false;
    role:      "Is price movement significant relative to window?";
}

atom volatility_gate {
    formula:   vol = stdev / mean;
    compute:   abs(signal - input) / max(input, 0.01);
    source:    "Risk Management";
    params:    { signal: 1.0, input: 1.0 };
    spectral:  [0, 100];
    broadband: true;
    role:      "Is volatility within acceptable bounds?";
}

ring market_watch {
    atoms: [price_momentum, volatility_gate, gouterman, marcus, boyer];
    purpose: "Track market signals. Boyer decides: trade or wait.";
}
```

### Example: Infrastructure Monitor

```
atom latency_resonance {
    formula:   score = baseline / actual;
    compute:   input / max(signal, 0.001);
    source:    "SRE Practice";
    params:    { input: 1.0, signal: 1.0 };
    spectral:  [1, 5000];
    broadband: false;
    role:      "Is latency within baseline expectations?";
}

ring infra_health {
    atoms: [latency_resonance, gouterman, mitchell, boyer];
    purpose: "Monitor infra signals. Mitchell accumulates evidence. Boyer alerts.";
}
```

## Compute Expression Rules

The compute expression is evaluated by `formula_eval.c` (interpreter) or `formula_jit.c` (LLVM JIT):

| Allowed | Example |
|---------|---------|
| Variables | `signal`, `input` (abs(signal)), param names |
| Arithmetic | `+`, `-`, `*`, `/`, `**` |
| Functions | `abs()`, `exp()`, `sqrt()`, `log()`, `min()`, `max()`, `pow()` |
| Constants | `pi` |
| Safe division | Denominator clamped to >= 1e-10 |
| Exp clamp | Argument clamped to [-20, 20] (`CSOS_EXP_CLAMP`) |

**NOT allowed:** Conditionals, loops, assignments, string operations, I/O.

## How to Deploy a New Spec

```bash
# 1. Write the spec
cat > specs/my_substrate.csos << 'EOF'
atom my_atom {
    formula:   f = signal * weight;
    compute:   signal * input;
    source:    "Custom";
    params:    { signal: 1.0, input: 1.0 };
    spectral:  [0, 10000];
    broadband: true;
}

ring my_ring {
    atoms: [my_atom, gouterman, boyer];
    purpose: "Custom substrate tracking";
}
EOF

# 2. Rebuild (spec is a Make dependency → auto-detects change)
make

# 3. Test
echo '{"action":"grow","ring":"my_ring"}' | ./csos
echo '{"action":"absorb","substrate":"my_thing","output":"data 42"}' | ./csos

# 4. Verify Law I
make validate
```

## The 3 Laws in Spec Context

**Law I**: Every atom carries its own `compute` expression. The code evaluates generically. No `if name == "X"` anywhere. Pre-commit hook verifies atom count = compute count.

**Law II**: Ring state persists to `.csos/rings/*.json`. Specs live in `specs/`. Volume-mountable.

**Law III**: Adding a substrate = adding an atom block. The 5 existing equations, with their generic evaluator, run on every substrate. Zero C changes.

## This Is NOT

- NOT an agent (no decision-making logic)
- NOT a build tool (no file creation)
- NOT a runtime (that's `./csos`)
- It IS documentation for how the spec system works, consulted by @csos-living when creating new substrates
