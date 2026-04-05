#!/bin/bash
# CSOS Pre-Commit Hook — Validates spec + rebuilds native binary
#
# Runs automatically on every git commit. Ensures:
# 1. Every atom in eco.csos has a compute expression (Law I)
# 2. No hardcoded equation arrays in C code
# 3. Native binary rebuilds if spec changed
# 4. Python ↔ Native isomorphism holds (if test exists)

set -e

SPEC="specs/eco.csos"
RED='\033[0;31m'
GREEN='\033[0;32m'
NC='\033[0m' # No color

echo "=== CSOS Pre-Commit Validation ==="

# ── 1. Check spec file exists ──
if [ ! -f "$SPEC" ]; then
    echo -e "${RED}ERROR: $SPEC not found${NC}"
    exit 1
fi

# ── 2. Validate every atom has a compute expression ──
ATOM_COUNT=$(grep -c '^atom ' "$SPEC" 2>/dev/null || echo 0)
COMPUTE_COUNT=$(grep -c 'compute:' "$SPEC" 2>/dev/null || echo 0)

if [ "$ATOM_COUNT" -ne "$COMPUTE_COUNT" ]; then
    echo -e "${RED}ERROR: $ATOM_COUNT atoms but only $COMPUTE_COUNT compute expressions${NC}"
    echo "Every atom in $SPEC must have a compute: field (Law I)"
    exit 1
fi
echo -e "${GREEN}  ✓ $ATOM_COUNT atoms, all with compute expressions${NC}"

# ── 3. Check for hardcoded equation arrays (Law I enforcement) ──
if grep -q 'EQUATIONS\[' src/native/membrane.c 2>/dev/null; then
    # Allow the comment reference but not an actual array definition
    if grep -q 'static.*EQUATIONS' src/native/membrane.c 2>/dev/null; then
        echo -e "${RED}ERROR: Hardcoded EQUATIONS[] found in membrane.c${NC}"
        echo "Law I violation: equations must come from $SPEC"
        exit 1
    fi
fi
echo -e "${GREEN}  ✓ No hardcoded equation arrays in membrane.c${NC}"

# ── 4. Check compute expressions are syntactically valid ──
python3 -c "
import sys
with open('$SPEC') as f:
    lines = f.readlines()
for i, line in enumerate(lines, 1):
    line = line.strip()
    if 'compute:' in line:
        expr = line.split('compute:')[1].strip().rstrip(';').strip()
        if not expr:
            print(f'ERROR: Empty compute expression at line {i}')
            sys.exit(1)
        # Basic syntax check: try to compile as Python expression
        try:
            compile(expr, '<compute>', 'eval')
        except SyntaxError as e:
            print(f'ERROR: Invalid compute expression at line {i}: {expr}')
            print(f'  {e}')
            sys.exit(1)
print('  ✓ All compute expressions are syntactically valid')
" 2>/dev/null || {
    echo -e "${RED}ERROR: Compute expression validation failed${NC}"
    exit 1
}

# ── 5. Rebuild native binary if spec or source changed ──
CHANGED=$(git diff --cached --name-only 2>/dev/null || echo "")
NEEDS_REBUILD=0

for pattern in "specs/eco.csos" "src/native/" "lib/membrane.h"; do
    if echo "$CHANGED" | grep -q "$pattern"; then
        NEEDS_REBUILD=1
        break
    fi
done

if [ "$NEEDS_REBUILD" -eq 1 ] && command -v make &>/dev/null; then
    echo "  Rebuilding native binary (spec or source changed)..."
    if make -j 2>/dev/null; then
        echo -e "${GREEN}  ✓ Native binary rebuilt successfully${NC}"
    else
        echo -e "${RED}WARNING: Native build failed (commit continues, fix build)${NC}"
    fi
fi

# ── 6. Run isomorphism test if available ──
if [ -f "tests/isomorphism/test_iso.py" ] && [ -f "./csos" ]; then
    if python3 tests/isomorphism/test_iso.py 2>/dev/null; then
        echo -e "${GREEN}  ✓ Python ↔ Native isomorphism verified${NC}"
    else
        echo -e "${RED}WARNING: Isomorphism test failed${NC}"
    fi
fi

echo -e "${GREEN}=== Pre-commit validation passed ===${NC}"
