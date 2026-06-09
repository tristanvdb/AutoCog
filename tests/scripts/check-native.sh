#!/usr/bin/env bash
#
###########################
#
# Native install check for AutoCog (developer / single-machine point of view).
# Same as check-release.sh — installs the way a user would, `pip install
# .[server,validate]` with deps resolved normally, then runs the smoke test —
# with two differences:
#
#   * AUTOCOG_TUNED is left unset, so the build tunes for the host architecture
#     (GGML_NATIVE on). This is faster at runtime but NOT portable to other CPUs;
#     check-release.sh forces AUTOCOG_TUNED=OFF for the portable-wheel path.
#   * The C/C++ compile runs in parallel via CMAKE_BUILD_PARALLEL_LEVEL (honored
#     by scikit-build-core's `cmake --build` invocation).
#
# Usage:
#   tests/scripts/check-native.sh [BUILD_DIR] [VENV_DIR]
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

# Parallelize the native build across all cores. cmake --build (which
# scikit-build-core drives) honors this for both the Make and Ninja generators.
export CMAKE_BUILD_PARALLEL_LEVEL="$(nproc 2>/dev/null || echo 4)"

cleanup() {
    if ! $BUILD_PERSISTENT; then rm -rf "$BUILD_DIR"; fi
    if ! $VENV_PERSISTENT; then rm -rf "$VENV_DIR"; fi
}
trap cleanup EXIT

echo "BUILD_DIR=$BUILD_DIR (persistent=$BUILD_PERSISTENT)"
echo "VENV_DIR=$VENV_DIR  (persistent=$VENV_PERSISTENT)"
echo "CMAKE_BUILD_PARALLEL_LEVEL=$CMAKE_BUILD_PARALLEL_LEVEL"
echo ""

# --- User-style install (deps resolved via the extras) --------------------
# Native build: AUTOCOG_TUNED is left at its default (host-tuned), unlike the
# portable check-release.sh which forces it OFF.
echo "=== Installing autocog[server,validate] (native, host-tuned) ==="
PREFIX_PATH=""
for d in "$REPO_ROOT/../opt" "$HOME/opt"; do
    [ -d "$d" ] && PREFIX_PATH="$d" && break
done
PIP_ARGS=(
    "$REPO_ROOT"[server,validate]
    --no-build-isolation
    --config-settings="build-dir=$BUILD_DIR"
    --config-settings="cmake.define.CMAKE_CXX_COMPILER_LAUNCHER=ccache"
    --config-settings="cmake.define.CMAKE_C_COMPILER_LAUNCHER=ccache"
)
[ -n "$PREFIX_PATH" ] && PIP_ARGS+=(--config-settings="cmake.define.CMAKE_PREFIX_PATH=$PREFIX_PATH")
pip install "${PIP_ARGS[@]}"

# --- Verify the install works ---------------------------------------------
"$SCRIPT_DIR/smoke.sh"

echo ""
echo "Native install check passed."
