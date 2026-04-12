#!/bin/bash
# CSOS Pre-Commit Hook — pure shell, zero Python dependency
#
# Validates on every commit:
#   1. eco.csos spec: every atom has a compute expression
#   2. membrane.c: no hardcoded equation arrays (Law I)
#   3. membrane.c: no bare magic numbers (all from membrane.h constants)
#   4. Rebuild binary if spec or source changed
#   5. Run 27 native tests
#
# Install: make hooks

set -e

SPEC="specs/eco.csos"
RED='\033[0;31m'
GREEN='\033[0;32m'
NC='\033[0m'

echo "=== CSOS Pre-Commit ==="

# ── 1. Spec exists ──
if [ ! -f "$SPEC" ]; then
    echo -e "${RED}FAIL: $SPEC not found${NC}"
    exit 1
fi

# ── 2. Every atom has a compute expression ──
ATOM_COUNT=$(grep -c '^atom ' "$SPEC" 2>/dev/null || echo 0)
COMPUTE_COUNT=$(grep -c 'compute:' "$SPEC" 2>/dev/null || echo 0)

if [ "$ATOM_COUNT" -ne "$COMPUTE_COUNT" ]; then
    echo -e "${RED}FAIL: $ATOM_COUNT atoms but $COMPUTE_COUNT compute expressions${NC}"
    exit 1
fi
echo -e "${GREEN}  ✓ $ATOM_COUNT atoms, all with compute expressions${NC}"

# ── 3. No hardcoded equation arrays ──
if grep -q 'static.*EQUATIONS' src/native/membrane.c 2>/dev/null; then
    echo -e "${RED}FAIL: Hardcoded EQUATIONS[] in membrane.c (Law I violation)${NC}"
    exit 1
fi
echo -e "${GREEN}  ✓ No hardcoded equation arrays${NC}"

# ── 4. No bare magic numbers in membrane.c ──
MAGIC=$(grep -E '0\.1[^0-9]|0\.02[^0-9]|0\.99[^0-9]|0\.833|0\.05[^0-9]| 0\.3[^0-9]' src/native/membrane.c 2>/dev/null | grep -v CSOS_ | grep -v '/\*' | grep -v '//' | grep -v '1e-10' | wc -l | tr -d ' ')
if [ "$MAGIC" -ne "0" ]; then
    echo -e "${RED}FAIL: $MAGIC bare magic numbers in membrane.c${NC}"
    grep -n -E '0\.1[^0-9]|0\.02[^0-9]|0\.99[^0-9]|0\.833|0\.05[^0-9]| 0\.3[^0-9]' src/native/membrane.c | grep -v CSOS_ | grep -v '/\*' | grep -v '//' | grep -v '1e-10'
    exit 1
fi
echo -e "${GREEN}  ✓ Zero bare magic numbers (all derived from equations)${NC}"

# ── 5. Rebuild if spec or source changed ──
CHANGED=$(git diff --cached --name-only 2>/dev/null || echo "")
NEEDS_REBUILD=0

for pattern in "specs/" "src/native/" "lib/"; do
    if echo "$CHANGED" | grep -q "$pattern"; then
        NEEDS_REBUILD=1
        break
    fi
done

if [ "$NEEDS_REBUILD" -eq 1 ] && command -v make &>/dev/null; then
    echo "  Rebuilding (source or spec changed)..."
    if make -j 2>/dev/null; then
        echo -e "${GREEN}  ✓ Binary rebuilt${NC}"
        # Stage the rebuilt binary
        git add csos 2>/dev/null || true
    else
        echo -e "${RED}FAIL: Build failed${NC}"
        exit 1
    fi
fi

# ── 6. Run native tests ──
if [ -f "./csos" ]; then
    if ./csos --test 2>&1 | grep -q "27/27 TESTS PASSED"; then
        echo -e "${GREEN}  ✓ 27/27 tests passed${NC}"
    else
        RESULT=$(./csos --test 2>&1 | tail -1)
        echo -e "${RED}FAIL: $RESULT${NC}"
        exit 1
    fi
fi

echo -e "${GREEN}=== Pre-commit passed ===${NC}"
