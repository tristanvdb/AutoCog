#!/usr/bin/env bash
#
###########################
#
# Coverage analysis for AutoCog: Debug build with instrumentation, run ctest +
# pytest, generate tests/coverage.md.
#
# Usage:
#   tests/scripts/tests-coverage.sh [BUILD_DIR] [VENV_DIR]
#
# BUILD_DIR: if supplied, reused across runs (ccache stays hot) and wiped at
#   start; if omitted, a temporary dir removed on exit.
# VENV_DIR:  if supplied, a venv you manage (reused, not removed); if omitted,
#   a temporary venv removed on exit.
#
# This top script owns environment setup + orchestration. The actual work lives
# in tests/scripts/steps/*.sh, which are shared with tests-release.sh and called
# step-by-step by the CI jobs. The venv's bin is prepended to PATH so the steps
# resolve pip/python/gcovr to it transparently (CI runs the steps against the
# system interpreter instead).
#
# Output: tests/coverage.md
#
###########################

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
STEPS="$SCRIPT_DIR/steps"

# --- BUILD_DIR ------------------------------------------------------------
if [[ -n "${1:-}" ]]; then
    export BUILD_DIR="$(readlink -f "$1")"
    BUILD_PERSISTENT=true
    rm -rf "$BUILD_DIR"; mkdir -p "$BUILD_DIR"
else
    export BUILD_DIR="$(mktemp -d -t autocog-build.XXXXXX)"
    BUILD_PERSISTENT=false
fi

# --- VENV_DIR -------------------------------------------------------------
if [[ -n "${2:-}" ]]; then
    VENV_DIR="$(readlink -f "$2")"
    VENV_PERSISTENT=true
    if [[ ! -f "$VENV_DIR/bin/python3" ]]; then
        echo "Error: VENV_DIR '$VENV_DIR' is not a venv (no bin/python3). Create with: python3 -m venv $VENV_DIR" >&2
        exit 1
    fi
else
    VENV_DIR="$(mktemp -d -t autocog-venv.XXXXXX)"
    VENV_PERSISTENT=false
    python3 -m venv "$VENV_DIR"
fi

# Seed test dependencies (deps-only) from the `test` dependency-group — single
# source of truth. Idempotent on a warm venv. steps/build.sh installs autocog
# itself with --no-deps.
"$VENV_DIR/bin/pip" install -q --upgrade pip
"$VENV_DIR/bin/pip" install -q --group "$REPO_ROOT/pyproject.toml:test" \
                                   scikit-build-core pybind11

# Expose the venv to the shared steps via PATH.
export PATH="$VENV_DIR/bin:$PATH"

cleanup() {
    if ! $BUILD_PERSISTENT; then rm -rf "$BUILD_DIR"; fi
    if ! $VENV_PERSISTENT; then rm -rf "$VENV_DIR"; fi
}
trap cleanup EXIT

echo "BUILD_DIR=$BUILD_DIR (persistent=$BUILD_PERSISTENT)"
echo "VENV_DIR=$VENV_DIR  (persistent=$VENV_PERSISTENT)"
echo ""

# --- Orchestration: Debug + coverage --------------------------------------
export BUILD_TYPE=Debug COVERAGE=ON
"$STEPS/build.sh"
"$STEPS/ctest.sh"
COV=1 "$STEPS/pytest.sh"
"$STEPS/cxx-report.sh"
"$STEPS/report.sh"

echo ""
echo "Coverage report: $REPO_ROOT/tests/coverage.md"
