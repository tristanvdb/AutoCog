#!/usr/bin/env bash
# The full v0.7 experiment: compute sweeps, then MCQ quality benchmarks,
# over every downloaded model; one results directory, one tarball.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
REPO="$(cd "$SCRIPT_DIR/../.." && pwd)"
OUT="$SCRIPT_DIR/results/$(date +%Y%m%d-%H%M%S)"
mkdir -p "$OUT"

{
    echo "host: $(hostname)"
    command -v nvidia-smi > /dev/null && nvidia-smi --query-gpu=name,memory.total,driver_version --format=csv,noheader
    echo "commit: $(git -C "$REPO" rev-parse HEAD)"
    echo "ngl: ${AUTOCOG_NGL:-99}"
} > "$OUT/machine.txt" 2>&1 || true

# Smoke first: the tiny model exercises the whole quality pipeline in minutes.
"$SCRIPT_DIR/run-quality.sh" "$OUT" "$REPO/models/tiny-llama3-test-Q2_K.gguf"

"$SCRIPT_DIR/run-compute.sh" "$OUT"
"$SCRIPT_DIR/run-quality.sh" "$OUT"

tar -czf "$OUT/../$(basename "$OUT")-results.tar.gz" -C "$OUT/.." "$(basename "$OUT")"
echo
echo "done. pull: $OUT/../$(basename "$OUT")-results.tar.gz"
