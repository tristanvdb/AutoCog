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
CAN_INSTALL=1
if [ "$(id -u)" -ne 0 ]; then
    if command -v sudo > /dev/null 2>&1; then
        SUDO="sudo"
    else
        echo "warning: not root and no sudo — cannot install packages" >&2
        CAN_INSTALL=0
    fi
fi

PKG_MGR=""
command -v apt-get > /dev/null 2>&1 && PKG_MGR=apt
[ -z "$PKG_MGR" ] && command -v dnf > /dev/null 2>&1 && PKG_MGR=dnf

# name : probe : apt package : dnf package
DEPS='
g++:g++:g++:gcc-c++
make:make:make:make
cmake:cmake:cmake:cmake
git:git:git:git
curl:curl:curl:curl
python3:python3:python3:python3
python3-venv:@venv:python3-venv:python3
python3-dev:@pydev:python3-dev:python3-devel
pip:@pip:python3-pip:python3-pip
ccache:ccache:ccache:ccache
unzip:unzip:unzip:unzip
'

probe() {  # probe NAME -> 0 if present
    case "$1" in
        @venv)  python3 -m venv --help > /dev/null 2>&1 ;;
        @pydev) command -v python3-config > /dev/null 2>&1 ;;
        @pip)   python3 -m pip --version > /dev/null 2>&1 ;;
        *)      command -v "$1" > /dev/null 2>&1 ;;
    esac
}

collect_missing() {  # fills MISSING_NAMES / MISSING_PKGS from the current state
    MISSING_NAMES=()
    MISSING_PKGS=()
    while IFS=: read -r name check aptpkg dnfpkg; do
        [ -z "$name" ] && continue
        if ! probe "$check"; then
            MISSING_NAMES+=("$name")
            case "$PKG_MGR" in
                apt) MISSING_PKGS+=("$aptpkg") ;;
                dnf) MISSING_PKGS+=("$dnfpkg") ;;
            esac
        fi
    done <<< "$DEPS"
}

collect_missing
if [ ${#MISSING_NAMES[@]} -eq 0 ]; then
    echo "toolchain complete — nothing to install"
elif [ -z "$PKG_MGR" ]; then
    echo "error: missing (${MISSING_NAMES[*]}) and no apt-get/dnf found — install manually, then re-run" >&2
    exit 1
elif [ "$CAN_INSTALL" -eq 0 ]; then
    echo "error: missing (${MISSING_NAMES[*]}) but cannot install (no root/sudo)" >&2
    exit 1
else
    echo "missing: ${MISSING_NAMES[*]}"
    readarray -t PKGS < <(printf '%s\n' "${MISSING_PKGS[@]}" | sort -u)
    case "$PKG_MGR" in
        apt) $SUDO apt-get update -qq
             $SUDO apt-get install -y -qq "${PKGS[@]}" ;;
        dnf) $SUDO dnf install -y -q "${PKGS[@]}" ;;
    esac
fi

# Re-probe and report every version; anything still missing is fatal.
echo "--- toolchain ---"
version_of() {
    case "$1" in
        g++)          g++ --version | head -1 ;;
        make)         make --version | head -1 ;;
        cmake)        cmake --version | head -1 ;;
        git)          git --version ;;
        curl)         curl --version | head -1 ;;
        python3)      python3 -V ;;
        python3-venv) echo "ok ($(python3 -V 2>&1))" ;;
        python3-dev)  echo "ok ($(python3-config --prefix))" ;;
        pip)          python3 -m pip --version ;;
        ccache)       ccache --version | head -1 ;;
        unzip)        unzip -v | head -1 ;;
    esac
}
FAIL=0
while IFS=: read -r name check _ _; do
    [ -z "$name" ] && continue
    if probe "$check"; then
        printf '  %-13s %s\n' "$name" "$(version_of "$name")"
    else
        printf '  %-13s MISSING\n' "$name"
        FAIL=1
    fi
done <<< "$DEPS"
if command -v nvcc > /dev/null 2>&1; then
    printf '  %-13s %s\n' "nvcc" "$(nvcc --version | grep -o 'release.*' | head -1)"
fi
if [ "$FAIL" -ne 0 ]; then
    echo "error: toolchain still incomplete after installation" >&2
    exit 1
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
    # Compile only for the GPU that is actually here: ggml's default is a
    # list of legacy architectures (sm_50..), which multiplies CUDA compile
    # time and spams nvcc deprecation warnings on CUDA >= 12.8.
    ARCH="$(nvidia-smi --query-gpu=compute_cap --format=csv,noheader 2>/dev/null | head -1 | tr -d '. ')"
    [ -n "$ARCH" ] && CUDA_ARGS+=("-DCMAKE_CUDA_ARCHITECTURES=$ARCH")
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
# pybind11 is a pyproject build-requires, so pip's isolated build env has it
# but the venv does not — and the standalone tools tree finds it through the
# active interpreter (import pybind11), so it must live in the venv too.
pip install pybind11 > /dev/null
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

"$EXP_DIR/downloader.sh"

echo
echo "setup complete. next: $(realpath --relative-to="$PWD" "$EXP_DIR" 2>/dev/null || echo "$EXP_DIR")/calibrate.sh"
