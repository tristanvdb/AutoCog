#!/usr/bin/env bash
#
# Shared pytest step. Runs the Python test suite. When COV=1, collects coverage
# (Python via pytest-cov into BUILD_DIR/coverage_py.json, C++ via .gcda from the
# instrumented bindings). Release runs leave COV unset (plain pytest).
#
# Environment:
#   BUILD_DIR   (required)  used for the coverage_py.json / pytest.log location
#   COV         (optional)  "1" to enable --cov; default off
#
# python is resolved from PATH; subprocesses spawned by tests (`autocog ...`,
# `golden_compare.py`) must resolve to the same interpreter, so PATH ordering
# is the caller's responsibility (top scripts prepend the venv bin).

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/../../.." && pwd)"

: "${BUILD_DIR:?BUILD_DIR must be set}"
BUILD_DIR="$(cd "$BUILD_DIR" && pwd)"  # absolute: used after cd "$REPO_ROOT"
COV="${COV:-}"

echo "=== Running pytest${COV:+ (with coverage)} ==="
cd "$REPO_ROOT"

PYTEST_ARGS=(tests/integration/modules tests/units/modules --timeout 120 -q)
if [ "$COV" = "1" ]; then
    PYTEST_ARGS+=(
        --cov=autocog
        --cov-config=pyproject.toml
        --cov-report=json:"$BUILD_DIR/coverage_py.json"
    )
fi

python -m pytest "${PYTEST_ARGS[@]}" 2>&1 | tee "$BUILD_DIR/pytest.log"

PASSED=$(grep -E "[0-9]+ passed" "$BUILD_DIR/pytest.log" | tail -1 | grep -oE "[0-9]+ passed" | head -1 | grep -oE "[0-9]+" || echo "0")
echo "pytest: $PASSED passed"
