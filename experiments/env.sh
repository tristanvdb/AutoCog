# Shared path resolution for experiment scripts. Source, don't execute.
#
# The convention: the repo is a subdirectory of your working directory and
# every artifact lands alongside it, not inside it —
#
#     ~/my-nfs/                  <- WORKDIR (where you invoke from)
#       autocog/                 <- the checkout (REPO)
#       .venv/  build-exp/  models/  datasets/  results/  .ccache/
#
# WORKDIR is the invocation directory; invoking from *inside* the repo
# falls back to the repo root (the old self-contained layout). Overrides:
#     AUTOCOG_WORKDIR   everything
#     MODELS_PATH       the models directory only
#     DATASETS_PATH     the datasets directory only
#     CCACHE_DIR        the compiler cache only

EXP_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO="$(cd "$EXP_DIR/.." && pwd)"

case "$PWD" in
    "$REPO"|"$REPO"/*) WORKDIR="$REPO" ;;
    *)                 WORKDIR="$PWD" ;;
esac
WORKDIR="${AUTOCOG_WORKDIR:-$WORKDIR}"

VENV="$WORKDIR/.venv"
BUILD_EXP="$WORKDIR/build-exp"
MODELS_DIR="${MODELS_PATH:-$WORKDIR/models}"
DATASETS_DIR="${DATASETS_PATH:-$WORKDIR/datasets}"
RESULTS_DIR="$WORKDIR/results"
export CCACHE_DIR="${CCACHE_DIR:-$WORKDIR/.ccache}"
