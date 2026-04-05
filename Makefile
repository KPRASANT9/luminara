# CSOS Build System — Spec-Driven Membrane + LLVM JIT
#
# The spec IS the code. All equations flow from specs/eco.csos.
# No hardcoded equation arrays — the parser reads the spec at build time.
#
# Build:     make              (with LLVM JIT + formula compilation)
# Build:     make nojit        (without LLVM, C-only with runtime formula eval)
# Codegen:   make codegen      (spec → LLVM IR offline compilation)
# Test:      make test
# Bench:     make bench
# Validate:  make validate     (spec syntax + isomorphism check)
# Clean:     make clean

# LLVM paths (brew install llvm)
LLVM_PREFIX  = /usr/local/opt/llvm
LLVM_CONFIG  = $(LLVM_PREFIX)/bin/llvm-config

# Detect LLVM
HAS_LLVM := $(shell test -x $(LLVM_CONFIG) && echo yes || echo no)

# Compiler: use LLVM's clang for JIT compatibility
ifeq ($(HAS_LLVM),yes)
  CC       = $(LLVM_PREFIX)/bin/clang
  CXX      = $(LLVM_PREFIX)/bin/clang++
  CFLAGS   = -Wall -Wextra -O2 -std=c11 -DCSOS_HAS_LLVM=1 \
             $(shell $(LLVM_CONFIG) --cflags 2>/dev/null | sed 's/-std=c++17//g')
  LDFLAGS  = $(shell $(LLVM_CONFIG) --ldflags) $(shell $(LLVM_CONFIG) --libs core orcjit native) \
             -lm -lc++ $(shell $(LLVM_CONFIG) --system-libs)
  JIT_SRC  = src/native/jit.c src/native/formula_jit.c
else
  CC       = cc
  CFLAGS   = -Wall -Wextra -O2 -std=c11
  LDFLAGS  = -lm
  JIT_SRC  =
endif

BIN      = csos
SPEC     = specs/eco.csos
RINGS    = csos-data/rings

# Source files — spec_parse and formula_eval are always compiled (no LLVM dep)
CORE_SRC = src/native/csos.c src/native/membrane.c src/native/protocol.c \
           src/native/page.c src/native/record.c src/native/ring.c src/native/store.c \
           src/native/spec_parse.c src/native/formula_eval.c
HEADERS  = lib/membrane.h lib/page.h lib/record.h lib/ring.h

.PHONY: all nojit clean test bench install codegen validate hooks

all: $(BIN)

# ═══ MAIN BUILD ═══
# Depends on spec file — changing an equation in eco.csos triggers rebuild
$(BIN): $(CORE_SRC) $(JIT_SRC) $(HEADERS) $(SPEC)
ifeq ($(HAS_LLVM),yes)
	@echo "Building with LLVM $(shell $(LLVM_CONFIG) --version) JIT + formula compilation..."
	$(CC) $(CFLAGS) -Ilib -Isrc/native -o $@ src/native/csos.c $(LDFLAGS)
else
	@echo "Building without LLVM (C-only, runtime formula eval)..."
	$(CC) $(CFLAGS) -Ilib -Isrc/native -o $@ src/native/csos.c $(LDFLAGS)
endif
	@echo ""
	@echo "Built: ./$(BIN)  [spec: $(SPEC)]"
	@echo "  Atoms loaded from spec at runtime (no hardcoded equations)"
	@echo "  Tests:     ./$(BIN) --test"
	@echo "  Benchmark: ./$(BIN) --bench"
	@echo "  HTTP:      ./$(BIN) --http 4096"

# ═══ C-ONLY BUILD (no LLVM, no JIT) ═══
nojit:
	cc -Wall -Wextra -O2 -std=c11 -Ilib -Isrc/native -o $(BIN) src/native/csos.c -lm
	@echo "Built: ./$(BIN)  (no JIT, runtime formula eval from spec)"

# ═══ OFFLINE CODEGEN (spec → LLVM IR) ═══
codegen: bin/csos_codegen
	@echo "Generating LLVM IR from spec..."
	./bin/csos_codegen $(SPEC) gen/csos.ll
	@echo "Generated: gen/csos.ll"

bin/csos_codegen: src/native/codegen/codegen.cpp $(SPEC)
ifeq ($(HAS_LLVM),yes)
	@mkdir -p bin gen
	$(CXX) -std=c++20 -O2 -o $@ $< \
	    $(shell $(LLVM_CONFIG) --cxxflags --ldflags --system-libs --libs core) \
	    -lpthread
	@echo "Built: bin/csos_codegen"
else
	@echo "ERROR: LLVM required for codegen. Install with: brew install llvm"
	@exit 1
endif

# ═══ VALIDATION ═��═
# Checks spec syntax and Python↔Native isomorphism
validate: $(BIN)
	@echo "=== Spec Validation ==="
	@python3 -c "\
	import json, sys; \
	lines = open('$(SPEC)').readlines(); \
	atoms = [l.strip().split()[1] for l in lines if l.strip().startswith('atom ')]; \
	computes = [l.strip() for l in lines if 'compute:' in l]; \
	print(f'  Atoms:    {len(atoms)} ({', '.join(atoms)})'); \
	print(f'  Computes: {len(computes)}'); \
	assert len(atoms) == len(computes), f'Every atom must have a compute expression ({len(atoms)} atoms, {len(computes)} computes)'; \
	print('  ✓ All atoms have compute expressions'); \
	"
	@echo ""
	@echo "=== Isomorphism Test (Python ↔ Native) ==="
	@python3 tests/isomorphism/test_iso.py
	@echo ""
	@echo "=== Native Smoke Test ==="
	@echo '{"action":"ping"}' | ./$(BIN)
	@echo '{"action":"absorb","substrate":"validate","output":"42.5 100"}' | ./$(BIN)
	@echo "=== All validations passed ==="

# ═══ TESTS ═══
test: $(BIN)
	@./$(BIN) --test
	@echo ""
	@echo "=== CLI Smoke Test ==="
	@echo '{"action":"ping"}' | ./$(BIN)
	@echo '{"action":"absorb","substrate":"smoke","output":"42.5 100"}' | ./$(BIN)

bench: $(BIN)
	@./$(BIN) --bench

# ���══ GIT HOOKS ═══
hooks:
	@echo "Installing pre-commit hook..."
	@cp scripts/pre-commit-csos.sh .git/hooks/pre-commit
	@chmod +x .git/hooks/pre-commit
	@echo "Installed: .git/hooks/pre-commit"

# ═══ CLEANUP ═══
clean:
	rm -f $(BIN) csos-native csos-debug
	rm -f bin/csos_codegen gen/csos.ll

install: $(BIN)
	cp $(BIN) /usr/local/bin/$(BIN)
	@echo "Installed: /usr/local/bin/$(BIN)"
