#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/../../.." && pwd)"

: "${BUILD_DIR:?BUILD_DIR must be set}"
: "${VENV_DIR:?VENV_DIR must be set}"

echo "=== Running pytest ==="
cd "$REPO_ROOT"

# Some tests spawn subprocesses (`autocog ...`, `python3 golden_compare.py ...`).
# Put the venv's bin first so those child processes resolve to the venv
# interpreter / console-script with the test deps installed.
export PATH="$VENV_DIR/bin:$PATH"

# autocog is installed in the venv (see build.sh). Run pytest with the venv's
# python so the installed package is found naturally — no PYTHONPATH gymnastics.
"$VENV_DIR/bin/python" -m pytest \
    tests/integration/modules tests/units/modules \
    --cov=autocog \
    --cov-config=pyproject.toml \
    --cov-report=json:"$BUILD_DIR/coverage_py.json" \
    --timeout 120 \
    -q 2>&1 | tee "$BUILD_DIR/pytest.log"

PASSED=$(grep -E "[0-9]+ passed" "$BUILD_DIR/pytest.log" | tail -1 | grep -oE "[0-9]+ passed" | head -1 | grep -oE "[0-9]+" || echo "0")
echo "pytest: $PASSED passed"
