#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/../../.." && pwd)"

: "${BUILD_DIR:?BUILD_DIR must be set}"
: "${VENV_DIR:?VENV_DIR must be set}"

# Find a prefix path for vendor dependencies (optional fast-path)
PREFIX_PATH=""
for d in "$REPO_ROOT/../opt" "$HOME/opt"; do
    [ -d "$d" ] && PREFIX_PATH="$d" && break
done

# Install autocog with coverage instrumentation into the venv.
# --no-build-isolation: use the venv's scikit-build-core/pybind11.
# --no-deps: only autocog itself; deps came from requirements.txt.
PIP_ARGS=(
    "$REPO_ROOT"[server]
    --no-build-isolation
    --no-deps
    --config-settings="build-dir=$BUILD_DIR"
    --config-settings="cmake.define.COVERAGE=ON"
    --config-settings="cmake.define.CMAKE_BUILD_TYPE=Debug"
    --config-settings="cmake.define.AUTOCOG_TUNED=OFF"
    --config-settings="cmake.define.CMAKE_CXX_COMPILER_LAUNCHER=ccache"
    --config-settings="cmake.define.CMAKE_C_COMPILER_LAUNCHER=ccache"
)
[ -n "$PREFIX_PATH" ] && PIP_ARGS+=(--config-settings="cmake.define.CMAKE_PREFIX_PATH=$PREFIX_PATH")

echo "=== Building (coverage-instrumented) ==="
"$VENV_DIR/bin/pip" install "${PIP_ARGS[@]}"
