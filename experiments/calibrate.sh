#!/usr/bin/env bash
# Calibration — the second of the two commands that start any experiment
# campaign. Two cells on the 1B model, one narrow (batch-of-one cost) and
# one wide (amortized cost): enough to fit per-token/per-call rates, verify
# GPU offload, and retune a campaign's budgets. Prints a block to paste
# back for tailoring the campaign command; ~3-6 minutes.
#
#   experiments/calibrate.sh            # default model (1B Instruct)
#   MODEL=models/foo.gguf experiments/calibrate.sh
set -euo pipefail

# shellcheck disable=SC1091
source "$(dirname "$0")/env.sh"
MODEL="${MODEL:-$MODELS_DIR/Llama-3.2-1B-Instruct-Q8_0.gguf}"
CAL="$RESULTS_DIR/calibration"
mkdir -p "$CAL"

export AUTOCOG_NGL="${AUTOCOG_NGL:-99}"
# shellcheck disable=SC1091
source "$VENV/bin/activate"

CELLS="$(mktemp)"
cat > "$CELLS" <<'EOF'
[
  {"beams": 1, "ahead": 1, "width": 1, "label": "narrow b1a1w1"},
  {"beams": 8, "ahead": 2, "width": 1, "label": "wide b8a2w1"}
]
EOF

python3 "$REPO/benchmarks/compute/sweep.py" --build "$BUILD_EXP" \
    --model "$MODEL" --cells "$CELLS" --tag calib --out "$CAL"
rm -f "$CELLS"

echo
echo "=================== CALIBRATION — paste this block back ==================="
{
    command -v nvidia-smi > /dev/null \
        && nvidia-smi --query-gpu=name,memory.total,utilization.gpu --format=csv,noheader
    echo "ngl: $AUTOCOG_NGL"
    python3 - "$CAL" <<'PYEOF'
import glob, json, sys
for path in sorted(glob.glob(sys.argv[1] + "/results-*calib.ndjson")):
    for line in open(path):
        r = json.loads(line)
        tok = r["autocog.perf.tokens.restore"] + r["autocog.perf.tokens.eval"]
        calls = r.get("autocog.perf.decode.calls", 0)
        print(f"{r['autocog.bench.model']:>28} {r.get('autocog.bench.label', '?'):>14}: "
              f"eval={r['autocog.perf.advance_seconds']:.2f}s "
              f"wall={r['autocog.bench.wall_seconds']:.2f}s tok={tok} calls={calls} "
              f"decode={r.get('autocog.perf.decode.seconds', 0):.2f}s "
              f"sample={r.get('autocog.perf.sample.seconds', 0):.2f}s")
PYEOF
} 2>&1
echo "==========================================================================="
echo
echo "next: launch your campaign, e.g."
echo "  nohup experiments/v0.7/run-perf-suite.sh 7 > perf-suite.log 2>&1 &"
