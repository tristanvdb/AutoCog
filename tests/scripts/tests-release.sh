#!/usr/bin/env bash
#
###########################
#
# Release-mode test run for AutoCog: Release build (no coverage instrumentation,
# no native tuning), run ctest + pytest. Catches bugs that a Debug/coverage
# build might mask (optimizer-dependent behavior, NDEBUG divergence).
#
# Usage:
#   tests/scripts/tests-release.sh [BUILD_DIR] [VENV_DIR]
#
# BUILD_DIR / VENV_DIR semantics identical to tests-coverage.sh.
#
# Setup + orchestration here; work in tests/scripts/steps/*.sh (shared). The
# only differences from tests-coverage.sh are BUILD_TYPE=Release, COVERAGE=OFF,
# pytest without --cov, and no gcovr/report.
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

# Same `test` group seeding as coverage: ctest's golden compares and the pytest
# suite need the harness deps; steps/build.sh installs autocog with --no-deps.
"$VENV_DIR/bin/pip" install -q --upgrade pip
"$VENV_DIR/bin/pip" install -q --group "$REPO_ROOT/pyproject.toml:test" \
                                   scikit-build-core pybind11

export PATH="$VENV_DIR/bin:$PATH"

cleanup() {
    if ! $BUILD_PERSISTENT; then rm -rf "$BUILD_DIR"; fi
    if ! $VENV_PERSISTENT; then rm -rf "$VENV_DIR"; fi
}
trap cleanup EXIT

echo "BUILD_DIR=$BUILD_DIR (persistent=$BUILD_PERSISTENT)"
echo "VENV_DIR=$VENV_DIR  (persistent=$VENV_PERSISTENT)"
echo ""

# --- Orchestration: Release, no coverage ----------------------------------
export BUILD_TYPE=Release COVERAGE=OFF
"$STEPS/build.sh"
"$STEPS/ctest.sh"
"$STEPS/pytest.sh"

echo ""
echo "Release tests passed."
