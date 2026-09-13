#!/usr/bin/env bash
#
# Campaign box setup + calibration — the entire operator surface.
#
# Follows the experiments workdir convention (share/experiments/env.sh):
# cd into your working directory (e.g. the Lambda persistent filesystem)
# with the repo cloned as autocog/ and artifacts beside it, then:
#
#     cd ~/my-nfs
#     autocog/share/benchmarks/campaigns/setup.sh            # setup + calibrate
#     autocog/share/benchmarks/campaigns/setup.sh --launch   # ... then run it
#
#     ~/my-nfs/
#       autocog/                      <- the checkout
#       .venv/  .ccache/              <- persist across instance restarts
#       models/                       <- your GGUFs (any naming)
#       datasets/                     <- raw ARC (auto-downloaded if absent)
#       results/v08-protocol/         <- everything the campaign produces
#
# Steps: (1) venv + CUDA Release install of autocog into WORKDIR/.venv
# (reused when already present with a CUDA build); (2) ARC located under
# WORKDIR/datasets (downloaded if missing), converted campaign question
# files written beside it; (3) the six Q8_0 GGUFs fuzzy-matched from
# WORKDIR/models and linked under the manifest names; (4) the sanity
# gate. The campaigns' datasets/, models/ and results/ entries become
# symlinks into WORKDIR, so all artifacts land on the NFS, not in the
# checkout.
#
# --launch execs the autopilot in the FOREGROUND (it prints progress and
# checkpoints continuously; if the terminal dies, rerun the same command
# — completed stages are skipped). Overrides: AUTOCOG_WORKDIR,
# MODELS_PATH, DATASETS_PATH (see env.sh), --cpu, --per-gpu N.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
# shellcheck disable=SC1091
source "$SCRIPT_DIR/../../experiments/env.sh"   # WORKDIR VENV MODELS_DIR DATASETS_DIR RESULTS_DIR REPO

NGL=99
CUDA=ON
LAUNCH=0
CALIBRATE=1
PER_GPU=4    # single-A100 topology: co-resident workers, VRAM-bound

while [ $# -gt 0 ]; do
    case "$1" in
        --cpu)     CUDA=OFF; NGL=0; shift ;;
        --launch)  LAUNCH=1; shift ;;
        --per-gpu) PER_GPU="$2"; shift 2 ;;
        --no-calibrate) CALIBRATE=0; shift ;;  # harness testing only
        *) echo "unknown option: $1" >&2; exit 2 ;;
    esac
done

echo "=== workdir: $WORKDIR (repo: $REPO) ==="

echo "=== [1/4] Python environment (CUDA=$CUDA) -> $VENV ==="
if [ ! -x "$VENV/bin/python3" ]; then
    python3 -m venv "$VENV"
fi
# shellcheck disable=SC1091
source "$VENV/bin/activate"
cuda_ok() {
    python3 - "$1" <<'EOF'
import sys
try:
    from autocog.backend.llama import backend_llama_cxx as b
except Exception:
    sys.exit(1)
want = sys.argv[1] == "ON"
sys.exit(0 if ("cuda:        yes" in b.build_info()) == want else 1)
EOF
}
if cuda_ok "$CUDA"; then
    echo "autocog already installed with the right backend — reusing"
else
    if command -v ccache > /dev/null 2>&1; then
        export CMAKE_CXX_COMPILER_LAUNCHER=ccache CMAKE_C_COMPILER_LAUNCHER=ccache
    fi
    pip install --upgrade pip > /dev/null
    CMAKE_ARGS="-DAUTOCOG_CUDA=$CUDA" pip install --force-reinstall "$REPO" \
        2>&1 | tail -3
    cuda_ok "$CUDA" || { echo "FATAL: install backend mismatch (wanted CUDA=$CUDA)"; exit 1; }
fi
python3 -c "from autocog.backend.llama import backend_llama_cxx as b; print(b.build_info())" \
    | grep -E "cuda|build_type"

echo "=== [2/4] Datasets -> $DATASETS_DIR ==="
mkdir -p "$DATASETS_DIR"
CONVERTED="$DATASETS_DIR/campaign-v08"
mkdir -p "$CONVERTED"
if [ ! -s "$CONVERTED/arc-easy.json" ] || [ ! -s "$CONVERTED/arc-challenge.json" ]; then
    EASY=$(find -L "$DATASETS_DIR" -name "ARC-Easy-Test.jsonl" 2>/dev/null | head -1)
    CHAL=$(find -L "$DATASETS_DIR" -name "ARC-Challenge-Test.jsonl" 2>/dev/null | head -1)
    if [ -z "$EASY" ] || [ -z "$CHAL" ]; then
        echo "downloading ARC (AI2 release) into $DATASETS_DIR ..."
        curl -fsSL -o "$DATASETS_DIR/arc.zip" \
            "https://ai2-public-datasets.s3.amazonaws.com/arc/ARC-V1-Feb2018.zip"
        (cd "$DATASETS_DIR" && unzip -q -o arc.zip && rm arc.zip)
        EASY=$(find -L "$DATASETS_DIR" -name "ARC-Easy-Test.jsonl" | head -1)
        CHAL=$(find -L "$DATASETS_DIR" -name "ARC-Challenge-Test.jsonl" | head -1)
    fi
    [ -n "$EASY" ] && [ -n "$CHAL" ] || { echo "ARC test jsonl not found under $DATASETS_DIR" >&2; exit 1; }
    python3 "$REPO/share/benchmarks/quality/convert.py" arc "$EASY" --out "$CONVERTED/arc-easy.json"
    python3 "$REPO/share/benchmarks/quality/convert.py" arc "$CHAL" --out "$CONVERTED/arc-challenge.json"
    python3 "$REPO/share/benchmarks/quality/convert.py" arc "$CHAL" --limit 500 \
        --out "$CONVERTED/arc-challenge-500.json"
fi
for f in arc-easy arc-challenge arc-challenge-500; do
    n=$(python3 -c "import json; print(len(json.load(open('$CONVERTED/$f.json'))))")
    echo "  $f.json: $n questions"
done
ln -sfn "$CONVERTED" "$SCRIPT_DIR/datasets"

echo "=== [3/4] Models from $MODELS_DIR ==="
LINKS="$MODELS_DIR/campaign-v08-links"
mkdir -p "$LINKS"
NEEDED="Llama-3.2-1B-Q8_0.gguf Llama-3.2-1B-Instruct-Q8_0.gguf
Llama-3.2-3B-Q8_0.gguf Llama-3.2-3B-Instruct-Q8_0.gguf
Llama-3.1-8B-Q8_0.gguf Llama-3.1-8B-Instruct-Q8_0.gguf"
MISSING=0
normalize() { echo "$1" | tr 'A-Z' 'a-z' | tr -d '_.-'; }
for want in $NEEDED; do
    if [ -e "$LINKS/$want" ]; then echo "  $want: present"; continue; fi
    wantn=$(normalize "${want%.gguf}")
    found=""
    while IFS= read -r cand; do
        candn=$(normalize "$(basename "${cand%.gguf}")")
        if [ "$candn" = "$wantn" ]; then found="$cand"; break; fi
        case "$candn" in *"$wantn"*) found="$cand" ;; esac
    done < <(find -L "$MODELS_DIR" -name "*.gguf" -not -path "$LINKS/*" | sort)
    if [ -n "$found" ]; then
        ln -sf "$found" "$LINKS/$want"
        echo "  $want -> $found"
    else
        echo "  $want: NOT FOUND under $MODELS_DIR"
        MISSING=1
    fi
done
[ "$MISSING" = 0 ] || { echo "FATAL: missing models above" >&2; exit 1; }
ln -sfn "$LINKS" "$SCRIPT_DIR/models"

mkdir -p "$RESULTS_DIR/v08-protocol"
ln -sfn "$RESULTS_DIR/v08-protocol" "$SCRIPT_DIR/results"
echo "results -> $RESULTS_DIR/v08-protocol"

if [ "$CALIBRATE" = 1 ]; then
    echo "=== [4/4] Calibrate (sanity gate) ==="
    cd "$REPO"
    python3 "$SCRIPT_DIR/client.py" "$SCRIPT_DIR/smoke-real.json" \
        --ngl "$NGL" --per-gpu "$PER_GPU" --sanity-only
else
    echo "=== [4/4] Calibrate SKIPPED (--no-calibrate) ==="
fi

echo ""
echo "=== Setup complete ==="
if [ "$LAUNCH" = 1 ]; then
    cd "$REPO"
    exec python3 "$SCRIPT_DIR/autopilot.py" --ngl "$NGL" --per-gpu "$PER_GPU"
else
    echo "run the campaign with:"
    echo "  source $VENV/bin/activate"
    echo "  python3 $SCRIPT_DIR/autopilot.py --ngl $NGL --per-gpu $PER_GPU"
    echo "(foreground; if the terminal dies, rerun — completed stages are skipped)"
fi
