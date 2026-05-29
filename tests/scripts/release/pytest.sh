#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/../../.." && pwd)"

: "${BUILD_DIR:?BUILD_DIR must be set}"
: "${VENV_DIR:?VENV_DIR must be set}"

echo "=== Running pytest (release) ==="
cd "$REPO_ROOT"

# Tests spawn subprocesses (`autocog ...`, `python3 golden_compare.py ...`);
# ensure they resolve to the venv interpreter / console-script.
export PATH="$VENV_DIR/bin:$PATH"

"$VENV_DIR/bin/python" -m pytest \
    tests/integration/modules tests/units/modules \
    --timeout 120 \
    -q 2>&1 | tee "$BUILD_DIR/pytest.log"

PASSED=$(grep -E "[0-9]+ passed" "$BUILD_DIR/pytest.log" | tail -1 | grep -oE "[0-9]+ passed" | head -1 | grep -oE "[0-9]+" || echo "0")
echo "pytest: $PASSED passed"
