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

# Machine profile: hardware inventory for this machine CONFIG, written on
# first run and reused by the campaign launcher (GPU count/topology). Keyed
# by a config hash so a re-imaged/re-shaped instance re-profiles while the
# same box skips it.
PROFILE_DIR="$WORKDIR/.calibration"
mkdir -p "$PROFILE_DIR"
CONFIG="$( { uname -sr; nproc; nvidia-smi --query-gpu=name,memory.total,driver_version \
             --format=csv,noheader 2>/dev/null || true; } 2>&1 )"
HASH=$(echo "$CONFIG" | sha256sum | cut -c1-12)
PROFILE="$PROFILE_DIR/profile-$HASH.json"
if [ -s "$PROFILE" ]; then
    echo "machine profile: $PROFILE (already profiled this config)"
else
    python3 - "$PROFILE" <<'PYEOF'
import json, os, subprocess, sys
prof = {"hostname": os.uname().nodename, "kernel": os.uname().release,
        "cpus": os.cpu_count(), "gpus": 0, "gpu_names": [], "vram_mb": [],
        "driver": None}
try:
    out = subprocess.run(["nvidia-smi", "--query-gpu=name,memory.total,driver_version",
                          "--format=csv,noheader,nounits"],
                         capture_output=True, text=True, timeout=15)
    for line in out.stdout.strip().splitlines():
        name, mem, drv = [c.strip() for c in line.split(",")]
        prof["gpu_names"].append(name)
        prof["vram_mb"].append(int(float(mem)))
        prof["driver"] = drv
    prof["gpus"] = len(prof["gpu_names"])
except (FileNotFoundError, subprocess.TimeoutExpired):
    pass
with open(sys.argv[1], "w") as f:
    json.dump(prof, f, indent=1)
print(f"machine profile written: {sys.argv[1]}")
print(json.dumps(prof))
PYEOF
fi

export AUTOCOG_NGL="${AUTOCOG_NGL:-99}"
# shellcheck disable=SC1091
source "$VENV/bin/activate"

# The installed package must match the machine: a non-CUDA build on a GPU
# box would silently run the whole campaign on CPU. The backend reports
# its build flags through the bindings (build_options).
GPUS=$(nvidia-smi -L 2>/dev/null | grep -c . || true)
python3 - "$GPUS" <<'PYEOF'
import json, sys
from autocog.backend.llama import backend_llama_cxx as b
opts = dict(b.build_options())
gpus = int(sys.argv[1] or 0)
print(f"package build: {json.dumps(opts)}")
if gpus > 0 and not (opts["cuda"] or opts["rocm"] or opts["vulkan"]):
    sys.exit(f"FATAL: {gpus} GPU(s) present but the installed autocog has no "
             "GPU backend -- rerun setup.sh (it enables CUDA when nvidia-smi "
             "is present) or reinstall with CMAKE_ARGS=-DAUTOCOG_CUDA=ON")
if opts["build_type"] != "Release":
    print(f"WARNING: package build_type={opts['build_type']} -- timing "
          "numbers will be meaningless")
PYEOF

CELLS="$(mktemp)"
cat > "$CELLS" <<'EOF'
[
  {"beams": 1, "ahead": 1, "width": 1, "label": "narrow b1a1w1"},
  {"beams": 8, "ahead": 2, "width": 1, "label": "wide b8a2w1"}
]
EOF

MODEL="$(realpath "$MODEL")"
( cd "$REPO" && python3 -m autocog bench perf --model "$MODEL" --cells "$CELLS" \
      --tag calib --out "$CAL" )
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
echo "next: run your campaign descriptor(s), e.g."
echo "  autocog/share/experiments/campaign.sh campaigns/v08-protocol.json"
