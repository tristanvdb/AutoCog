#!/bin/bash
# Coverage analysis for AutoCog (C++ via gcovr, Python via pytest-cov)
#
# Usage:
#   tests/coverage.sh [BUILD_DIR]
#
# Default BUILD_DIR: builds/autocog-cov (relative to repo root)
# Requires: gcovr, pytest, pytest-cov (see tests/requirements.txt)
#
# Output: coverage summary table to stdout

set -e

REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD_DIR="${1:-$REPO_ROOT/builds/autocog-cov}"

# Find cmake prefix path (vendor dependencies)
if [ -d "$REPO_ROOT/../opt" ]; then
    PREFIX_PATH="$REPO_ROOT/../opt"
elif [ -d "$HOME/opt" ]; then
    PREFIX_PATH="$HOME/opt"
else
    PREFIX_PATH=""
fi

CMAKE_ARGS="-DCMAKE_BUILD_TYPE=Debug -DCOVERAGE=ON"
if [ -n "$PREFIX_PATH" ]; then
    CMAKE_ARGS="$CMAKE_ARGS -DCMAKE_PREFIX_PATH=$PREFIX_PATH"
fi

# ============================================================================
# Step 1: Configure and build
# ============================================================================

echo "=== Configuring (coverage build) ==="
mkdir -p "$BUILD_DIR"
cd "$BUILD_DIR"
cmake $CMAKE_ARGS "$REPO_ROOT" 2>&1 | tail -3

echo "=== Building ==="
cmake --build . -j$(nproc) 2>&1 | tail -3

# ============================================================================
# Step 2: Run C++ tests (ctest)
# ============================================================================

echo "=== Running ctest ==="
# Clear old coverage data
find . -name "*.gcda" -delete

ctest -j$(nproc) --timeout 30 --output-on-failure 2>&1 | grep -E "tests passed|tests failed"

# ============================================================================
# Step 3: Run Python tests (pytest-cov → JSON)
# ============================================================================
# Run pytest BEFORE gcovr so that C++ coverage from binding calls
# (e.g. real model tests) is included in the .gcda files.

echo "=== Running pytest ==="
PYTHONPATH="$BUILD_DIR/bindings/compiler-stl:$BUILD_DIR/bindings/runtime-sta:$BUILD_DIR/bindings/backend-llama"
export PYTHONPATH
export BUILD_DIR

PY_COV="$BUILD_DIR/coverage_py.json"
cd "$REPO_ROOT"
python3 -m pytest tests/integration/modules tests/units/modules \
    --cov=autocog \
    --cov-report=json:"$PY_COV" \
    -q 2>&1 | grep -E "passed|failed|error"

# ============================================================================
# Step 4: Generate C++ coverage (gcovr → JSON)
# ============================================================================
# gcovr reads .gcda files accumulated from both ctest AND pytest.

echo "=== Generating C++ coverage ==="
cd "$BUILD_DIR"
CXX_COV="$BUILD_DIR/coverage_cxx.json"
gcovr -r "$REPO_ROOT" . \
    --filter "$REPO_ROOT/libs/" \
    --filter "$REPO_ROOT/tools/" \
    --filter "$REPO_ROOT/bindings/" \
    --gcov-ignore-errors=no_working_dir_found \
    --json "$CXX_COV" \
    2>/dev/null

# ============================================================================
# Step 5: Generate combined report
# ============================================================================

REPORT="$REPO_ROOT/tests/coverage.md"
python3 "$REPO_ROOT/tests/coverage_report.py" "$CXX_COV" "$PY_COV" -o "$REPORT"
echo "=== Coverage report: $REPORT ==="
