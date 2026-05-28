#!/bin/bash
# Coverage analysis for AutoCog (C++ via gcovr, Python via pytest-cov)
#
# Usage:
#   tests/coverage.sh [BUILD_DIR]
#
# Default BUILD_DIR: builds/autocog-cov (relative to repo root)
# Requires: gcovr, pytest, pytest-cov (see tests/requirements.txt)
#
# Output: tests/coverage.md

set -e

REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD_DIR="$(readlink -f "${1:-$REPO_ROOT/builds/autocog-cov}")"

# Find cmake prefix path (vendor dependencies)
PREFIX_PATH=""
for d in "$REPO_ROOT/../opt" "$HOME/opt"; do
    [ -d "$d" ] && PREFIX_PATH="$d" && break
done

CMAKE_ARGS="-DCMAKE_BUILD_TYPE=Debug -DCOVERAGE=ON"
CMAKE_ARGS="$CMAKE_ARGS -DCMAKE_CXX_COMPILER_LAUNCHER=ccache"
CMAKE_ARGS="$CMAKE_ARGS -DCMAKE_C_COMPILER_LAUNCHER=ccache"
[ -n "$PREFIX_PATH" ] && CMAKE_ARGS="$CMAKE_ARGS -DCMAKE_PREFIX_PATH=$PREFIX_PATH"

PIP_CFG="--config-settings=build-dir=$BUILD_DIR"
PIP_CFG="$PIP_CFG --config-settings=cmake.define.COVERAGE=ON"
PIP_CFG="$PIP_CFG --config-settings=cmake.define.CMAKE_BUILD_TYPE=Debug"
PIP_CFG="$PIP_CFG --config-settings=cmake.define.CMAKE_CXX_COMPILER_LAUNCHER=ccache"
PIP_CFG="$PIP_CFG --config-settings=cmake.define.CMAKE_C_COMPILER_LAUNCHER=ccache"
[ -n "$PREFIX_PATH" ] && PIP_CFG="$PIP_CFG --config-settings=cmake.define.CMAKE_PREFIX_PATH=$PREFIX_PATH"

# ============================================================================
# Step 1: Configure and build (coverage-instrumented)
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
find . -name "*.gcda" -delete
ctest -j$(nproc) --timeout 120 --output-on-failure 2>&1 | grep -E "tests passed|tests failed"

# ============================================================================
# Step 3: Install to isolated target dir, run pytest
# ============================================================================
# pip install --target puts the coverage-instrumented package in a temp dir.
# We temporarily disable the editable install so PYTHONPATH takes priority.
# The build-dir reuse makes this fast (no rebuild).

echo "=== Installing to coverage target ==="
COV_TARGET=$(mktemp -d)

# Find and temporarily disable the editable install's .pth file
PTH_FILE=$(find /usr/local/lib /usr/lib -name "_autocog_editable.pth" 2>/dev/null | head -1)

cleanup() {
    # Restore editable install
    [ -n "$PTH_FILE" ] && [ -f "${PTH_FILE}.cov-bak" ] && mv "${PTH_FILE}.cov-bak" "$PTH_FILE"
    rm -rf "$COV_TARGET"
}
trap cleanup EXIT

pip install --target="$COV_TARGET" --no-deps --no-build-isolation \
    $PIP_CFG "$REPO_ROOT" 2>&1 | tail -3

# Disable editable install so PYTHONPATH wins
[ -n "$PTH_FILE" ] && mv "$PTH_FILE" "${PTH_FILE}.cov-bak"

echo "=== Running pytest ==="
cd "$REPO_ROOT"
PYTHONPATH="$COV_TARGET" python3 -m pytest tests/integration/modules tests/units/modules \
    --cov=autocog \
    --cov-config=pyproject.toml \
    --cov-report=json:"$BUILD_DIR/coverage_py.json" \
    -q 2>&1 | grep -E "passed|failed|error"

# ============================================================================
# Step 4: Generate C++ coverage (gcovr → JSON)
# ============================================================================
# gcovr reads .gcda files accumulated from both ctest AND pytest.

echo "=== Generating C++ coverage ==="
cd "$BUILD_DIR"
gcovr -r "$REPO_ROOT" . \
    --filter "$REPO_ROOT/libs/" \
    --filter "$REPO_ROOT/tools/" \
    --filter "$REPO_ROOT/bindings/" \
    --gcov-ignore-errors=no_working_dir_found \
    --json "$BUILD_DIR/coverage_cxx.json" \
    2>/dev/null

# ============================================================================
# Step 5: Generate combined report
# ============================================================================

REPORT="$REPO_ROOT/tests/coverage.md"
python3 "$REPO_ROOT/tests/coverage_report.py" \
    "$BUILD_DIR/coverage_cxx.json" "$BUILD_DIR/coverage_py.json" \
    -o "$REPORT"
echo "Coverage report written to $REPORT"
