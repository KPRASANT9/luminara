# CSOS Build System — F->0 Architecture
#
# F = COMPLEXITY - ACCURACY. dF/dt <= 0 at all times.
# Every line of code serves this guarantee. Nothing else exists.
#
# make              Build (C-only, runtime formula eval)
# make test         Run stress tests
# make bench        Benchmark membrane_absorb() throughput
# make validate     Verify spec consistency + Law I enforcement
# make clean        Remove build artifacts
# make seed         Build + plant the seed
# make http         Build + start HTTP daemon on port 4200

CC       = cc
CFLAGS   = -Wall -Wextra -O2 -std=c11
LDFLAGS  = -lm

BIN      = csos
SPEC     = specs/eco.csos
SRC      = src/native/csos.c
HEADERS  = lib/membrane.h

.PHONY: all clean test bench validate hooks http seed full

all: $(BIN)

$(BIN): $(SRC) $(HEADERS) $(SPEC) $(wildcard src/native/*.c)
	@echo "Building CSOS (F->0 architecture)..."
	$(CC) $(CFLAGS) -Ilib -Isrc/native -o $@ $(SRC) $(LDFLAGS)
	@echo ""
	@echo "Built: ./$(BIN)  [spec: $(SPEC)]"
	@echo "  Tests:     ./$(BIN) --test"
	@echo "  Benchmark: ./$(BIN) --bench"
	@echo "  HTTP:      ./$(BIN) --http 4200"

validate: $(BIN)
	@echo "=== Spec Validation ==="
	@ATOMS=$$(grep -c '^atom ' $(SPEC)); \
	 COMPUTES=$$(grep -c 'compute:' $(SPEC)); \
	 echo "  Atoms: $$ATOMS  Computes: $$COMPUTES"; \
	 if [ "$$ATOMS" -ne "$$COMPUTES" ]; then echo "FAIL: atom/compute mismatch"; exit 1; fi; \
	 echo "  All atoms have compute expressions"
	@echo ""
	@echo "=== Law I: No hardcoded equations ==="
	@if grep -q 'static.*EQUATIONS' src/native/membrane.c; then echo "FAIL: hardcoded EQUATIONS[]"; exit 1; fi
	@echo "  No hardcoded equation arrays"
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

full: $(BIN) test validate
	@echo ""
	@echo "=== FULL BUILD COMPLETE ==="
	@echo "  Binary:     ./$(BIN)"
	@echo "  Tests:      PASSED"
	@echo "  Validation: PASSED"

hooks:
	@cp scripts/pre-commit-csos.sh .git/hooks/pre-commit
	@chmod +x .git/hooks/pre-commit
	@echo "Installed: .git/hooks/pre-commit"

clean:
	rm -f $(BIN) csos-native csos-debug
