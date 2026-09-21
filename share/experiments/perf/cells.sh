#!/usr/bin/env bash
#
# Run one cells file against one model, on a machine you control.
#
#     autocog/share/experiments/perf/cells.sh <cells> [MODEL.gguf|--rng] [extra args...]
#
# <cells> is a name under share/benchmarks/compute (bottleneck, termination,
# mech, workload) or a path to a cells JSON file.
#
# No scheduling, no workers, no concurrency: an isolated in-process run, which
# is the only way the timings mean anything.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck disable=SC1091
source "$SCRIPT_DIR/../bootstrap.sh"

CELLS="${1:?usage: cells.sh <cells> [MODEL.gguf|--rng] [args...]}"
shift
if [ ! -f "$CELLS" ]; then
    CELLS="$REPO/share/benchmarks/compute/cells-$CELLS.json"
    [ -f "$CELLS" ] || { echo "no such cells file: $1" >&2; exit 1; }
fi

MODEL_ARGS=()
if [ "${1:-}" = "--rng" ]; then
    shift
elif [ -n "${1:-}" ] && [ -f "${1:-}" ]; then
    MODEL_ARGS=(--model "$1"); shift
fi

OUT="${OUT:-$WORKDIR/results/perf}"
mkdir -p "$OUT"
echo "=== perf: $(basename "$CELLS") -> $OUT ==="
exec python3 -m autocog bench perf --cells "$CELLS" --out "$OUT" \
    "${MODEL_ARGS[@]}" "$@"
