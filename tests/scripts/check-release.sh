#!/usr/bin/env bash
#
###########################
#
# Release install check for AutoCog (user point of view). Installs the project
# the way an end user would — `pip install .[server,validate]`, pulling deps
# normally — then runs the install smoke test. No test framework, no ctest.
#
# Usage:
#   tests/scripts/check-release.sh [BUILD_DIR] [VENV_DIR]
#
# BUILD_DIR / VENV_DIR semantics as in the other top scripts. Unlike the test
# scripts, the venv is left bare (no `test` group): the build resolves deps via
# the extras, mirroring a real user install. ccache + --no-build-isolation are
# CI-practicality concessions; the dep path is what mimics the user.
#
###########################

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"

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

# Bare venv: only pip + the build backend (scikit-build-core/pybind11 are build
# requirements, not runtime deps). Runtime deps come from the extras below.
"$VENV_DIR/bin/pip" install -q --upgrade pip
"$VENV_DIR/bin/pip" install -q scikit-build-core pybind11

export PATH="$VENV_DIR/bin:$PATH"

cleanup() {
    if ! $BUILD_PERSISTENT; then rm -rf "$BUILD_DIR"; fi
    if ! $VENV_PERSISTENT; then rm -rf "$VENV_DIR"; fi
}
trap cleanup EXIT

echo "BUILD_DIR=$BUILD_DIR (persistent=$BUILD_PERSISTENT)"
echo "VENV_DIR=$VENV_DIR  (persistent=$VENV_PERSISTENT)"
echo ""

# --- User-style install (deps resolved via the extras) --------------------
echo "=== Installing autocog[server,validate] (user path) ==="
PREFIX_PATH=""
for d in "$REPO_ROOT/../opt" "$HOME/opt"; do
    [ -d "$d" ] && PREFIX_PATH="$d" && break
done
PIP_ARGS=(
    "$REPO_ROOT"[server,validate]
    --no-build-isolation
    --config-settings="build-dir=$BUILD_DIR"
    --config-settings="cmake.define.AUTOCOG_TUNED=OFF"
    --config-settings="cmake.define.CMAKE_CXX_COMPILER_LAUNCHER=ccache"
    --config-settings="cmake.define.CMAKE_C_COMPILER_LAUNCHER=ccache"
)
[ -n "$PREFIX_PATH" ] && PIP_ARGS+=(--config-settings="cmake.define.CMAKE_PREFIX_PATH=$PREFIX_PATH")
pip install "${PIP_ARGS[@]}"

# --- Verify the install works ---------------------------------------------
"$SCRIPT_DIR/smoke.sh"

echo ""
echo "Release install check passed."
