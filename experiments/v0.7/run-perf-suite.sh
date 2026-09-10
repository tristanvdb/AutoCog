#!/usr/bin/env bash
# The unattended performance suite: E1-E5 in one command after setup.sh.
#
#   experiments/v0.7/run-perf-suite.sh [TOTAL_HOURS]     (default: 7)
#
# Each experiment gets a slice of the total budget and stops launching new
# cells when its slice is spent (completed cells are always kept — results
# append per cell). Retuning after calibration is arguments/env only:
#
#   E1_BUDGET..E4_BUDGET   per-experiment seconds (override the split)
#   MODEL_1B, MODEL_3B     model paths (defaults below)
#   E5_QUESTIONS           questions per cell in the accuracy-budget probe
#
# Experiments:
#   E1  core beams x ahead x width matrix on 1B (minus the two b8 w2
#       monster cells) — the CPU-vs-GPU era series
#   E2  mechanism ablations on 1B: KV slot-pool curve, queue metrics,
#       and the topk x beams cross
#   E3  workload scaling on 1B: completion length (+repetition penalty),
#       prompt prefix size, vocab mask size (the CPU-side sampling suspect)
#   E4  3B anchor cells — size scaling per effect class
#   E5  accuracy-budget probe: a timed slice of the quality benchmark
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
# shellcheck disable=SC1091
source "$SCRIPT_DIR/../env.sh"
TOTAL_HOURS="${1:-7}"
TOTAL_S=$(python3 -c "print(int($TOTAL_HOURS * 3600))")

MODEL_1B="${MODEL_1B:-$MODELS_DIR/Llama-3.2-1B-Instruct-Q8_0.gguf}"
MODEL_3B="${MODEL_3B:-$MODELS_DIR/Llama-3.2-3B-Instruct-Q8_0.gguf}"
BUILD="$BUILD_EXP"
CELLS="$SCRIPT_DIR/cells"
OUT="$RESULTS_DIR/perf-$(date +%Y%m%d-%H%M%S)"
mkdir -p "$OUT"

export AUTOCOG_NGL="${AUTOCOG_NGL:-99}"
# shellcheck disable=SC1091
source "$VENV/bin/activate"

# Budget split (seconds); override any via env.
E1_BUDGET="${E1_BUDGET:-$(( TOTAL_S * 35 / 100 ))}"
E2_BUDGET="${E2_BUDGET:-$(( TOTAL_S * 20 / 100 ))}"
E3_BUDGET="${E3_BUDGET:-$(( TOTAL_S * 20 / 100 ))}"
E4_BUDGET="${E4_BUDGET:-$(( TOTAL_S * 15 / 100 ))}"
E5_QUESTIONS="${E5_QUESTIONS:-10}"

{
    echo "host: $(hostname)"
    command -v nvidia-smi > /dev/null && nvidia-smi --query-gpu=name,memory.total,driver_version --format=csv,noheader
    echo "commit: $(git -C "$REPO" rev-parse HEAD)"
    echo "ngl: $AUTOCOG_NGL  total_hours: $TOTAL_HOURS"
    echo "budgets: E1=$E1_BUDGET E2=$E2_BUDGET E3=$E3_BUDGET E4=$E4_BUDGET E5_questions=$E5_QUESTIONS"
} > "$OUT/machine.txt" 2>&1 || true
cat "$OUT/machine.txt"

sweep() {  # sweep NAME BUDGET MODEL CELLS_FILE
    local name="$1" budget="$2" model="$3" cellsf="$4"
    echo "=== $name (budget ${budget}s, $(basename "$model")) ==="
    # One experiment failing must not take down the unattended suite.
    python3 "$REPO/benchmarks/compute/sweep.py" \
        --build "$BUILD" --model "$model" --cells "$cellsf" \
        --budget-seconds "$budget" --tag "$name" --out "$OUT" \
        2>&1 | tee "$OUT/$name.log" \
        || echo "!!! $name failed — continuing" | tee -a "$OUT/$name.log"
}

sweep e1 "$E1_BUDGET" "$MODEL_1B" "$CELLS/e1-core.json"
sweep e2 "$E2_BUDGET" "$MODEL_1B" "$CELLS/e2-mech.json"
sweep e3 "$E3_BUDGET" "$MODEL_1B" "$CELLS/e3-workload.json"

if [ -s "$MODEL_3B" ]; then
    sweep e4 "$E4_BUDGET" "$MODEL_3B" "$CELLS/e4-3b.json"
else
    echo "=== e4 skipped: $MODEL_3B not found ===" | tee "$OUT/e4.log"
fi

echo "=== e5: accuracy-budget probe ==="
python3 "$REPO/benchmarks/quality/run.py" \
    --build "$BUILD" --model "$MODEL_1B" \
    --syntaxes default,special --demos select,select-cot \
    --questions "$E5_QUESTIONS" --out "$OUT/e5-1b" 2>&1 | tee "$OUT/e5.log" \
    || echo "!!! e5 (1B) failed — continuing" | tee -a "$OUT/e5.log"
if [ -s "$MODEL_3B" ]; then
    python3 "$REPO/benchmarks/quality/run.py" \
        --build "$BUILD" --model "$MODEL_3B" \
        --syntaxes default --demos select \
        --questions 5 --out "$OUT/e5-3b" 2>&1 | tee -a "$OUT/e5.log" \
        || echo "!!! e5 (3B) failed — continuing" | tee -a "$OUT/e5.log"
fi

tar -czf "$OUT/../$(basename "$OUT")-results.tar.gz" -C "$OUT/.." "$(basename "$OUT")"
echo
echo "done. pull: $OUT/../$(basename "$OUT")-results.tar.gz"
