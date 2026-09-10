#!/usr/bin/env bash
# Machine setup — the first of the two commands that start any experiment
# campaign (then: calibrate.sh, then the campaign's script). Run it from
# your working directory with the repo as a subdirectory:
#
#     cd ~/my-nfs && autocog/experiments/setup.sh
#
# Artifacts (.venv, build-exp, models, results, .ccache) land alongside
# the repo — see env.sh for the layout and overrides. Safe to re-run and
# networked-FS aware: persisted models/ccache are reused, while a venv or
# build tree stamped by a different machine/toolchain is rebuilt.
#
# Assumes a clean clone with submodules initialized:
#   git submodule update --init --recursive
set -euo pipefail

# shellcheck disable=SC1091
source "$(dirname "$0")/env.sh"

if [ ! -e "$REPO/vendors/llama/include/llama.h" ]; then
    echo "vendored submodules missing — run: git -C $REPO submodule update --init --recursive" >&2
    exit 1
fi

echo "=== system dependencies ==="
SUDO=""
if [ "$(id -u)" -ne 0 ]; then
    if command -v sudo > /dev/null 2>&1; then
        SUDO="sudo"
    else
        echo "warning: not root and no sudo — skipping package installation" >&2
    fi
fi

need_tools() {  # commands that must exist before we build
    for c in g++ make cmake git curl python3; do
        command -v "$c" > /dev/null 2>&1 || return 0
    done
    python3 -m venv --help > /dev/null 2>&1 || return 0
    return 1
}

if need_tools; then
    if command -v apt-get > /dev/null 2>&1; then
        $SUDO apt-get update -qq
        $SUDO apt-get install -y -qq build-essential cmake git curl \
            python3-venv python3-dev python3-pip ccache
    elif command -v dnf > /dev/null 2>&1; then
        $SUDO dnf install -y -q gcc-c++ make cmake git curl \
            python3-devel python3-pip
        $SUDO dnf install -y -q ccache || true   # EPEL-only on RHEL-likes; optional
    else
        echo "warning: no apt-get/dnf found — install a C++ toolchain, cmake, curl," >&2
        echo "         and python3 (with venv) manually, then re-run" >&2
        exit 1
    fi
else
    echo "toolchain present — nothing to install"
fi

# CUDA toolkit is never auto-installed (driver/toolkit setup is image
# business); we only diagnose the half-configured case.
if command -v nvidia-smi > /dev/null 2>&1 && ! command -v nvcc > /dev/null 2>&1; then
    echo "warning: GPU present but nvcc (CUDA toolkit) missing — the CUDA build" >&2
    echo "         will likely fail; use a CUDA-toolkit image or install it first" >&2
fi

CUDA_ARGS=()
if command -v nvidia-smi > /dev/null 2>&1; then
    echo "=== CUDA GPU detected — building with GGML_CUDA ==="
    nvidia-smi --query-gpu=name,memory.total --format=csv,noheader || true
    CUDA_ARGS=(-DGGML_CUDA=ON)
else
    echo "=== no GPU detected — CPU build ==="
fi

# The workdir may live on a networked FS and persist across (different)
# machines: models and the ccache survive — a win — but a venv whose
# interpreter changed and a CMake cache pinning another machine's
# compilers/CUDA must be detected and rebuilt, not trusted.
STAMP="$(uname -sr) py=$(python3 -V 2>&1) gxx=$(g++ -dumpversion 2>/dev/null) cuda=${CUDA_ARGS[*]:-none}"

echo "=== python venv + package (Release) into $VENV ==="
if [ -d "$VENV" ]; then
    if ! "$VENV/bin/python3" -c pass > /dev/null 2>&1 \
       || [ "$(cat "$VENV/.autocog-stamp" 2>/dev/null)" != "$STAMP" ]; then
        echo "stale venv (machine/python changed) — recreating"
        rm -rf "$VENV"
    fi
fi
python3 -m venv "$VENV"
# shellcheck disable=SC1091
source "$VENV/bin/activate"
pip install --upgrade pip > /dev/null
CMAKE_ARGS="${CUDA_ARGS[*]:-}" pip install "$REPO"   # local-dir install: always rebuilt
echo "$STAMP" > "$VENV/.autocog-stamp"

echo "=== Release tools tree into $BUILD_EXP ==="
if [ -f "$BUILD_EXP/CMakeCache.txt" ] \
   && [ "$(cat "$BUILD_EXP/.autocog-stamp" 2>/dev/null)" != "$STAMP" ]; then
    echo "stale build tree (machine/toolchain changed) — wiping"
    rm -rf "$BUILD_EXP"
fi
LAUNCHER_ARGS=()
command -v ccache > /dev/null 2>&1 && \
    LAUNCHER_ARGS=(-DCMAKE_CXX_COMPILER_LAUNCHER=ccache -DCMAKE_C_COMPILER_LAUNCHER=ccache)
cmake -B "$BUILD_EXP" -S "$REPO" \
    -DCMAKE_BUILD_TYPE=Release \
    -DAUTOCOG_BUILD_TESTS=OFF \
    "${LAUNCHER_ARGS[@]}" \
    "${CUDA_ARGS[@]}"
cmake --build "$BUILD_EXP" --target autocog_stlc autocog_ista autocog_xfta autocog_psta autocog_efta -j"$(nproc)"
echo "$STAMP" > "$BUILD_EXP/.autocog-stamp"

echo "=== models into $MODELS_DIR ==="
"$EXP_DIR/models.sh"

echo
echo "setup complete. next: $(realpath --relative-to="$PWD" "$EXP_DIR" 2>/dev/null || echo "$EXP_DIR")/calibrate.sh"
