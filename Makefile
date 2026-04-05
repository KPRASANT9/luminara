# CSOS Build System — Unified Membrane + LLVM JIT
#
# Build:     make              (with LLVM JIT)
# Build:     make nojit        (without LLVM, C-only fallback)
# Test:      make test
# Bench:     make bench
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
  JIT_SRC  = src/native/jit.c
else
  CC       = cc
  CFLAGS   = -Wall -Wextra -O2 -std=c11
  LDFLAGS  = -lm
  JIT_SRC  =
endif

BIN = csos

.PHONY: all nojit clean test bench install

all: $(BIN)

$(BIN): src/native/csos.c src/native/membrane.c src/native/protocol.c \
        src/native/page.c src/native/record.c src/native/ring.c src/native/store.c \
        $(JIT_SRC) lib/membrane.h lib/page.h lib/record.h lib/ring.h
ifeq ($(HAS_LLVM),yes)
	@echo "Building with LLVM $(shell $(LLVM_CONFIG) --version) JIT..."
	$(CC) $(CFLAGS) -Ilib -Isrc/native -o $@ src/native/csos.c $(LDFLAGS)
else
	@echo "Building without LLVM (C-only, no JIT)..."
	$(CC) $(CFLAGS) -Ilib -Isrc/native -o $@ src/native/csos.c $(LDFLAGS)
endif
	@echo ""
	@echo "Built: ./$(BIN)"
	@echo "  Tests:     ./$(BIN) --test"
	@echo "  Benchmark: ./$(BIN) --bench"
	@echo "  HTTP:      ./$(BIN) --http 4096"

# C-only build (no LLVM, no JIT)
nojit:
	cc -Wall -Wextra -O2 -std=c11 -Ilib -Isrc/native -o $(BIN) src/native/csos.c -lm
	@echo "Built: ./$(BIN)  (no JIT)"

test: $(BIN)
	@./$(BIN) --test
	@echo ""
	@echo "=== CLI Smoke Test ==="
	@echo '{"action":"ping"}' | ./$(BIN)
	@echo '{"action":"absorb","substrate":"smoke","output":"42.5 100"}' | ./$(BIN)

bench: $(BIN)
	@./$(BIN) --bench

clean:
	rm -f $(BIN) csos-native csos-debug

install: $(BIN)
	cp $(BIN) /usr/local/bin/$(BIN)
	@echo "Installed: /usr/local/bin/$(BIN)"
