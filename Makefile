# CSOS Build System — Single native binary from .csos specs
#
# The spec IS the code. Equations flow from specs/eco.csos → spec_parse.c → membrane.
# Constants flow from membrane.h (all derived from 5 equations, zero magic numbers).
#
# make              Build with LLVM JIT (formula compilation + absorb vectorization)
# make nojit        Build without LLVM (C-only, runtime formula eval)
# make test         Run 27 stress tests
# make bench        Benchmark membrane_absorb() throughput
# make validate     Verify spec consistency + Law I enforcement
# make hooks        Install git pre-commit hook
# make clean        Remove build artifacts
# make seed         Build + plant the seed (one command, everything grows)
# make http         Build + start HTTP daemon on port 4200

# ═══ LLVM detection ═══
LLVM_PREFIX  = /usr/local/opt/llvm
LLVM_CONFIG  = $(LLVM_PREFIX)/bin/llvm-config
HAS_LLVM := $(shell test -x $(LLVM_CONFIG) && echo yes || echo no)

ifeq ($(HAS_LLVM),yes)
  CC       = $(LLVM_PREFIX)/bin/clang
  CFLAGS   = -Wall -Wextra -O2 -std=c11 -DCSOS_HAS_LLVM=1 \
             $(shell $(LLVM_CONFIG) --cflags 2>/dev/null | sed 's/-std=c++17//g')
  LDFLAGS  = $(shell $(LLVM_CONFIG) --ldflags) $(shell $(LLVM_CONFIG) --libs core orcjit native) \
             -lm -lc++ $(shell $(LLVM_CONFIG) --system-libs)
else
  CC       = cc
  CFLAGS   = -Wall -Wextra -O2 -std=c11
  LDFLAGS  = -lm
endif

# ═══ RDMA detection ═══
HAS_RDMA := $(shell pkg-config --exists libibverbs 2>/dev/null && echo yes || echo no)
ifeq ($(HAS_RDMA),yes)
  CFLAGS  += -DCSOS_HAS_RDMA=1 $(shell pkg-config --cflags libibverbs)
  LDFLAGS += $(shell pkg-config --libs libibverbs)
endif

BIN      = csos
SPEC     = specs/eco.csos
SRC      = src/native/csos.c
HEADERS  = lib/membrane.h lib/page.h lib/record.h lib/ring.h

.PHONY: all nojit clean test bench validate hooks http seed pgo-gen pgo-use wasm arm64 rdma-test ir full

all: $(BIN)

# ═══ BUILD ═══
# Depends on spec — changing an equation triggers rebuild
$(BIN): $(SRC) $(HEADERS) $(SPEC) $(wildcard src/native/*.c)
ifeq ($(HAS_LLVM),yes)
	@echo "Building with LLVM $(shell $(LLVM_CONFIG) --version) JIT..."
else
	@echo "Building C-only (no LLVM)..."
endif
	$(CC) $(CFLAGS) -Ilib -Isrc/native -o $@ $(SRC) $(LDFLAGS)
	@echo ""
	@echo "Built: ./$(BIN)  [spec: $(SPEC)]"
	@echo "  Tests:     ./$(BIN) --test"
	@echo "  Benchmark: ./$(BIN) --bench"
	@echo "  HTTP:      ./$(BIN) --http 4096"

nojit:
	cc -Wall -Wextra -O2 -std=c11 -Ilib -Isrc/native -o $(BIN) $(SRC) -lm
	@echo "Built: ./$(BIN)  (no JIT)"

# ═══ VALIDATE ═══
# Pure shell — no Python dependency
validate: $(BIN)
	@echo "=== Spec Validation ==="
	@ATOMS=$$(grep -c '^atom ' $(SPEC)); \
	 COMPUTES=$$(grep -c 'compute:' $(SPEC)); \
	 echo "  Atoms: $$ATOMS  Computes: $$COMPUTES"; \
	 if [ "$$ATOMS" -ne "$$COMPUTES" ]; then echo "FAIL: atom/compute mismatch"; exit 1; fi; \
	 echo "  ✓ All atoms have compute expressions"
	@echo ""
	@echo "=== Law I: No hardcoded equations ==="
	@if grep -q 'static.*EQUATIONS' src/native/membrane.c; then echo "FAIL: hardcoded EQUATIONS[]"; exit 1; fi
	@echo "  ✓ No hardcoded equation arrays"
	@echo ""
	@echo "=== Zero magic numbers ==="
	@MAGIC=$$(grep -E '0\.1[^0-9]|0\.02[^0-9]|0\.99[^0-9]|0\.833|0\.05[^0-9]| 0\.3[^0-9]' src/native/membrane.c | grep -v CSOS_ | grep -v '/\*' | grep -v '//' | grep -v 1e-10 | wc -l | tr -d ' '); \
	 if [ "$$MAGIC" -ne "0" ]; then echo "FAIL: $$MAGIC bare magic numbers in membrane.c"; exit 1; fi; \
	 echo "  ✓ Zero bare magic numbers in membrane.c"
	@echo ""
	@echo "=== Native Tests ==="
	@./$(BIN) --test 2>&1 | tail -1
	@echo ""
	@echo "=== All validations passed ==="

test: $(BIN)
	@./$(BIN) --test

bench: $(BIN)
	@./$(BIN) --bench

seed: $(BIN)
	./$(BIN) --seed

http: $(BIN)
	./$(BIN) --http 4200

# ═══ PROFILE-GUIDED BUILD ═══
pgo-gen: clean
	$(CC) $(CFLAGS) -fprofile-generate -Ilib -Isrc/native -o $(BIN) $(SRC) $(LDFLAGS)
	@echo "Built with PGO instrumentation. Run workload, then: make pgo-use"

pgo-use:
	$(CC) $(CFLAGS) -fprofile-use -Ilib -Isrc/native -o $(BIN) $(SRC) $(LDFLAGS)
	@echo "Built with PGO optimization."

# ═══ MULTI-TARGET ═══
wasm:
	emcc -O2 -std=c11 -Ilib -Isrc/native -o csos.wasm $(SRC) -s EXPORTED_FUNCTIONS='["_csos_handle"]' -s MODULARIZE=1
	@echo "Built: csos.wasm (WebAssembly target)"

arm64:
	$(CC) $(CFLAGS) -target arm64-apple-macos11 -Ilib -Isrc/native -o csos-arm64 $(SRC) $(LDFLAGS)
	@echo "Built: csos-arm64"

# ═══ RDMA ═══
rdma-test: $(BIN)
	@echo "=== RDMA Status ==="
	@echo '{"action":"rdma","sub":"status"}' | ./$(BIN) 2>/dev/null | grep "^{" | python3 -m json.tool 2>/dev/null || echo '{"rdma":"not available"}'

# ═══ IR INSPECT ═══
ir: $(BIN)
	@echo '{"action":"ir","detail":"full"}' | ./$(BIN) 2>/dev/null | grep "^{" | tail -1 | python3 -m json.tool 2>/dev/null || echo '{"error":"binary not running"}'

# ═══ ALL-IN-ONE ═══
full: $(BIN) test validate
	@echo ""
	@echo "=== FULL BUILD COMPLETE ==="
	@echo "  Binary:     ./$(BIN)"
	@echo "  Tests:      PASSED"
	@echo "  Validation: PASSED"
ifeq ($(HAS_LLVM),yes)
	@echo "  JIT:        LLVM $(shell $(LLVM_CONFIG) --version)"
else
	@echo "  JIT:        disabled (no LLVM)"
endif
ifeq ($(HAS_RDMA),yes)
	@echo "  RDMA:       enabled"
else
	@echo "  RDMA:       tcp_fallback"
endif
	@echo "  Modes:      Visual | Code | Exec"
	@echo "  Agents:     @csos-living → @csos-visual | @csos-codegen | @csos-runtime"
	@echo "  IR:         make ir"
	@echo "  HTTP:       make http"

# ═══ GIT HOOKS ═══
hooks:
	@cp scripts/pre-commit-csos.sh .git/hooks/pre-commit
	@chmod +x .git/hooks/pre-commit
	@echo "Installed: .git/hooks/pre-commit"

clean:
	rm -f $(BIN) csos-native csos-debug
