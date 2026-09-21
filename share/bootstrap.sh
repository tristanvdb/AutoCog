# Bring up the environment a campaign or a perf run needs. Source, don't run.
#
# Idempotent: every step checks before acting, so this is safe to source on a
# machine that is already set up, and safe to re-source after a crash. It has
# to be shell because it creates the venv that holds autocog — a Python app
# cannot bootstrap the interpreter environment it is importing from.
#
# Sets: REPO, WORKDIR, VENV.

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

if [ ! -x "$VENV/bin/python3" ]; then
    echo "=== creating $VENV ==="
    python3 -m venv "$VENV"
fi
# shellcheck disable=SC1091
source "$VENV/bin/activate"

# A local-directory install always rebuilds, which is what we want: the venv
# must hold the checkout that is about to run, not whatever was there before.
# A machine that cannot reach PyPI but already has a working install is a
# normal situation (an offline box, a re-run) and is not a reason to refuse;
# a machine that can do neither is.
bootstrap_install() {
    python3 -m pip install --quiet --upgrade pip || return 1
    python3 -m pip install --quiet pybind11 pyyaml || return 1
    local cuda=""
    if command -v nvcc > /dev/null 2>&1 || [ -d /usr/local/cuda ]; then
        cuda="-DGGML_CUDA=ON"
    fi
    echo "=== installing autocog from $REPO ==="
    CMAKE_ARGS="$cuda" python3 -m pip install --quiet "$REPO[server]" || return 1
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
