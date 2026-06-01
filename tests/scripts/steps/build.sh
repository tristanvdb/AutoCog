#!/usr/bin/env bash
#
# Shared build step: install autocog (native extensions) into the active
# environment. Used by tests-coverage.sh and tests-release.sh, and called
# directly by the matching CI jobs.
#
# Environment:
#   BUILD_DIR   (required)  cmake build directory (config-settings build-dir)
#   BUILD_TYPE  (optional)  CMAKE_BUILD_TYPE; default "Debug"
#   COVERAGE    (optional)  "ON"/"OFF"; default "OFF"
#
# Tooling (pip/python) is resolved from PATH. Top scripts put a venv's bin on
# PATH; CI uses the system interpreter. Either way, the venv/system must already
# have the build backend (scikit-build-core, pybind11) and the runtime deps
# (the `test` group, or the extras) installed — this step uses --no-deps.
#
# We install ".[server,validate]" rather than plain "." so the command documents
# what the build is expected to contain. --no-deps makes the extras inert here
# (deps come from the environment seeding), but keeps the intent explicit.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/../../.." && pwd)"

: "${BUILD_DIR:?BUILD_DIR must be set}"
BUILD_TYPE="${BUILD_TYPE:-Debug}"
COVERAGE="${COVERAGE:-OFF}"

# Vendor dependency fast-path (use a prebuilt prefix if present).
PREFIX_PATH=""
for d in "$REPO_ROOT/../opt" "$HOME/opt"; do
    [ -d "$d" ] && PREFIX_PATH="$d" && break
done

PIP_ARGS=(
    "$REPO_ROOT"[server,validate]
    --no-build-isolation
    --no-deps
    --config-settings="build-dir=$BUILD_DIR"
    --config-settings="cmake.define.CMAKE_BUILD_TYPE=$BUILD_TYPE"
    --config-settings="cmake.define.COVERAGE=$COVERAGE"
    --config-settings="cmake.define.AUTOCOG_TUNED=OFF"
    --config-settings="cmake.define.CMAKE_CXX_COMPILER_LAUNCHER=ccache"
    --config-settings="cmake.define.CMAKE_C_COMPILER_LAUNCHER=ccache"
)
[ -n "$PREFIX_PATH" ] && PIP_ARGS+=(--config-settings="cmake.define.CMAKE_PREFIX_PATH=$PREFIX_PATH")

echo "=== Building (BUILD_TYPE=$BUILD_TYPE COVERAGE=$COVERAGE) ==="
pip install "${PIP_ARGS[@]}"
