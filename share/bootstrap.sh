# Bring up the environment a campaign or a perf run needs. Source, don't run.
#
# Idempotent: every step checks before acting, so this is safe to source on a
# machine that is already set up, and safe to re-source after a crash. It has
# to be shell because it creates the venv that holds autocog — a Python app
# cannot bootstrap the interpreter environment it is importing from.
#
# Sets: REPO, WORKDIR, VENV.
#
# GPU backend: AUTOCOG_GPU_BACKEND=auto|cuda|vulkan|rocm|cpu (default auto: CUDA
# with a CUDA toolkit, else Vulkan with an AMD GPU, else CPU). ROCm is never
# chosen automatically. Toolchains are never installed from here.

set -euo pipefail

REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

# Artifacts live beside the checkout, never inside it. Invoking from within
# the checkout falls back to its parent so a development tree stays clean.
case "$PWD" in
    "$REPO"|"$REPO"/*) WORKDIR="$(dirname "$REPO")" ;;
    *)                 WORKDIR="$PWD" ;;
esac
WORKDIR="${AUTOCOG_WORKDIR:-$WORKDIR}"
VENV="$WORKDIR/venv"
export CCACHE_DIR="${CCACHE_DIR:-$WORKDIR/ccache}"

# Checks activate, not python3: an interrupted or failed creation leaves the
# interpreter behind without it.
if [ ! -f "$VENV/bin/activate" ]; then
    echo "=== creating $VENV ==="
    python3 -m venv --clear "$VENV"
fi
# shellcheck disable=SC1091
source "$VENV/bin/activate"

amd_gpus() {  # sysfs device directories, one per line
    local vendor
    for vendor in /sys/class/drm/card[0-9]*/device/vendor; do
        [ -r "$vendor" ] && [ "$(cat "$vendor")" = 0x1002 ] && dirname "$vendor"
    done
    return 0
}

GPU_BACKEND="${AUTOCOG_GPU_BACKEND:-auto}"
if [ "$GPU_BACKEND" = auto ]; then
    if command -v nvcc > /dev/null 2>&1 || [ -d /usr/local/cuda ]; then GPU_BACKEND=cuda
    elif [ -n "$(amd_gpus)" ]; then GPU_BACKEND=vulkan
    else GPU_BACKEND=cpu
    fi
fi
GPU_ARGS=""
case "$GPU_BACKEND" in
    cuda) GPU_ARGS="-DAUTOCOG_CUDA=ON" ;;
    vulkan)
        if ! command -v glslc > /dev/null 2>&1 \
           || [ ! -e /usr/include/vulkan/vulkan.h ] \
           || [ ! -e /usr/include/spirv/unified1/spirv.hpp ]; then
            echo "FATAL: the Vulkan build needs glslc, Vulkan and SPIR-V headers, and a driver." >&2
            echo "  apt: sudo apt install glslc libvulkan-dev spirv-headers mesa-vulkan-drivers vulkan-tools" >&2
            echo "  dnf: sudo dnf install glslc vulkan-loader-devel spirv-headers-devel mesa-vulkan-drivers vulkan-tools" >&2
            echo "  or AUTOCOG_GPU_BACKEND=cpu to build without a GPU" >&2
            exit 1
        fi
        GPU_ARGS="-DAUTOCOG_VULKAN=ON" ;;
    rocm)
        if ! command -v hipconfig > /dev/null 2>&1; then
            echo "FATAL: AUTOCOG_GPU_BACKEND=rocm but hipconfig not found -- install ROCm first" >&2
            exit 1
        fi
        export HIPCXX="${HIPCXX:-$(hipconfig -l)/clang}" HIP_PATH="${HIP_PATH:-$(hipconfig -R)}"
        GPU_ARGS="-DAUTOCOG_ROCM=ON"
        # Only the ISA of the GPU that is here.
        GPU_TARGET="$(rocminfo 2>/dev/null | grep -oE 'gfx[0-9a-f]+' | head -1 || true)"
        [ -n "$GPU_TARGET" ] && GPU_ARGS="$GPU_ARGS -DGPU_TARGETS=$GPU_TARGET" ;;
    cpu) ;;
    *) echo "FATAL: AUTOCOG_GPU_BACKEND=$GPU_BACKEND (want auto|cuda|vulkan|rocm|cpu)" >&2; exit 1 ;;
esac

# Unified memory: the GPU is limited by the kernel's GTT size, by default half
# of RAM, well below what these machines are bought for.
for card in $(amd_gpus); do
    [ -r "$card/mem_info_gtt_total" ] || continue
    GTT_GB=$(( $(cat "$card/mem_info_gtt_total") / 1000000000 ))
    RAM_GB=$(( $(awk '/MemTotal/ {print $2}' /proc/meminfo) / 1000000 ))
    if [ "$GTT_GB" -lt $(( RAM_GB * 3 / 4 )) ]; then
        echo "WARNING: GTT is ${GTT_GB} GB of ${RAM_GB} GB RAM -- large models will not fit; see docs/strix-halo.md" >&2
    fi
done

# A local-directory install always rebuilds, which is what we want: the venv
# must hold the checkout that is about to run, not whatever was there before.
# A machine that cannot reach PyPI but already has a working install is a
# normal situation (an offline box, a re-run) and is not a reason to refuse;
# a machine that can do neither is.
bootstrap_install() {
    python3 -m pip install --quiet --upgrade pip || return 1
    python3 -m pip install --quiet pybind11 pyyaml || return 1
    echo "=== installing autocog from $REPO (GPU backend: $GPU_BACKEND) ==="
    CMAKE_ARGS="$GPU_ARGS" python3 -m pip install --quiet "$REPO[server]" || return 1
}

if ! bootstrap_install; then
    if python3 -c "import autocog" > /dev/null 2>&1; then
        echo "WARNING: install failed; using the autocog already in $VENV" >&2
    else
        echo "FATAL: cannot install autocog and none is present in $VENV" >&2
        exit 1
    fi
fi

export PYTHONPATH="$REPO/share${PYTHONPATH:+:$PYTHONPATH}"
