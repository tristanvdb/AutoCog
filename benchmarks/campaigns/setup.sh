#!/usr/bin/env bash
#
# Campaign box setup + calibration — the entire operator surface.
#
#   benchmarks/campaigns/setup.sh --models /path/to/ggufs [--arc /path/to/arc]
#                                 [--venv DIR] [--cpu] [--launch]
#
# Does everything between a bare clone and a running campaign:
#   1. venv + CUDA Release install of autocog (ccache-warmed rebuilds)
#   2. datasets: ARC jsonl -> questions.json (from --arc, else downloads
#      the AI2 release zip)
#   3. models: link the six Q8_0 GGUFs from --models under models/ by
#      the manifest names (fuzzy match on normalized filenames)
#   4. calibrate: client.py smoke-real.json --ngl 99 --sanity-only
#      (affinity, VRAM delta, GPU utilization, probe run, per worker)
#
# Ends by printing the autopilot line — or launches it directly under
# nohup with --launch, making the whole campaign one command:
#
#   benchmarks/campaigns/setup.sh --models /data/ggufs --launch
#
# Idempotent: existing venv/datasets/links are reused, so rerunning
# after a failure only redoes what is missing.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"

MODELS_SRC=""
ARC_SRC=""
VENV_DIR="$REPO_ROOT/venv-campaign"
NGL=99
CUDA=ON
LAUNCH=0
CALIBRATE=1

while [ $# -gt 0 ]; do
    case "$1" in
        --models) MODELS_SRC="$2"; shift 2 ;;
        --arc)    ARC_SRC="$2"; shift 2 ;;
        --venv)   VENV_DIR="$2"; shift 2 ;;
        --cpu)    CUDA=OFF; NGL=0; shift ;;
        --launch) LAUNCH=1; shift ;;
        --no-calibrate) CALIBRATE=0; shift ;;  # harness testing only
        *) echo "unknown option: $1" >&2; exit 2 ;;
    esac
done
[ -n "$MODELS_SRC" ] || { echo "--models /path/to/ggufs is required" >&2; exit 2; }

echo "=== [1/4] Python environment (CUDA=$CUDA) ==="
if [ ! -x "$VENV_DIR/bin/python3" ]; then
    python3 -m venv "$VENV_DIR"
fi
# shellcheck disable=SC1091
source "$VENV_DIR/bin/activate"
if ! python3 -c "import autocog" 2>/dev/null; then
    if command -v ccache > /dev/null 2>&1; then
        export CMAKE_CXX_COMPILER_LAUNCHER=ccache CMAKE_C_COMPILER_LAUNCHER=ccache
        export CCACHE_DIR="${CCACHE_DIR:-$REPO_ROOT/.ccache}"
    fi
    pip install --upgrade pip > /dev/null
    CMAKE_ARGS="-DAUTOCOG_CUDA=$CUDA" pip install "$REPO_ROOT" \
        2>&1 | tail -3
else
    echo "autocog already installed in $VENV_DIR — reusing"
fi
python3 -c "from autocog.backend.llama import backend_llama_cxx as b; print(b.build_info())" \
    | grep -E "cuda|build_type"
if [ "$CUDA" = "ON" ]; then
    python3 -c "from autocog.backend.llama import backend_llama_cxx as b; import sys; \
info = b.build_info(); sys.exit(0 if 'cuda:        yes' in info else 1)" \
        || { echo "FATAL: install is not a CUDA build (pass --cpu to accept)"; exit 1; }
fi

echo "=== [2/4] Datasets ==="
DATASETS="$SCRIPT_DIR/datasets"
mkdir -p "$DATASETS"
if [ ! -s "$DATASETS/arc-easy.json" ] || [ ! -s "$DATASETS/arc-challenge.json" ]; then
    if [ -z "$ARC_SRC" ]; then
        ARC_SRC="$SCRIPT_DIR/.arc-download"
        if [ ! -d "$ARC_SRC/ARC-V1-Feb2018-2" ]; then
            echo "downloading ARC (AI2 release)..."
            mkdir -p "$ARC_SRC"
            curl -fsSL -o "$ARC_SRC/arc.zip" \
                "https://ai2-public-datasets.s3.amazonaws.com/arc/ARC-V1-Feb2018.zip"
            (cd "$ARC_SRC" && unzip -q arc.zip)
        fi
        ARC_SRC="$ARC_SRC/ARC-V1-Feb2018-2"
    fi
    EASY=$(find "$ARC_SRC" -name "ARC-Easy-Test.jsonl" | head -1)
    CHAL=$(find "$ARC_SRC" -name "ARC-Challenge-Test.jsonl" | head -1)
    [ -n "$EASY" ] && [ -n "$CHAL" ] || { echo "ARC test jsonl not found under $ARC_SRC" >&2; exit 1; }
    python3 "$REPO_ROOT/benchmarks/quality/convert.py" arc "$EASY" --out "$DATASETS/arc-easy.json"
    python3 "$REPO_ROOT/benchmarks/quality/convert.py" arc "$CHAL" --out "$DATASETS/arc-challenge.json"
    python3 "$REPO_ROOT/benchmarks/quality/convert.py" arc "$CHAL" --limit 500 \
        --out "$DATASETS/arc-challenge-500.json"
fi
for f in arc-easy arc-challenge arc-challenge-500; do
    n=$(python3 -c "import json; print(len(json.load(open('$DATASETS/$f.json'))))")
    echo "  $f.json: $n questions"
done

echo "=== [3/4] Models ==="
MODELS="$SCRIPT_DIR/models"
mkdir -p "$MODELS"
# The names the manifests expect; matched fuzzily (case/dash/underscore
# insensitive substring) against *.gguf under --models.
NEEDED="Llama-3.2-1B-Q8_0.gguf Llama-3.2-1B-Instruct-Q8_0.gguf
Llama-3.2-3B-Q8_0.gguf Llama-3.2-3B-Instruct-Q8_0.gguf
Llama-3.1-8B-Q8_0.gguf Llama-3.1-8B-Instruct-Q8_0.gguf"
MISSING=0
normalize() { echo "$1" | tr 'A-Z' 'a-z' | tr -d '_.-'; }
for want in $NEEDED; do
    if [ -e "$MODELS/$want" ]; then echo "  $want: present"; continue; fi
    wantn=$(normalize "${want%.gguf}")
    found=""
    while IFS= read -r cand; do
        candn=$(normalize "$(basename "${cand%.gguf}")")
        # exact normalized match, or candidate contains the wanted name
        # while an instruct/base mismatch is excluded
        if [ "$candn" = "$wantn" ]; then found="$cand"; break; fi
        case "$candn" in *"$wantn"*) found="$cand" ;; esac
    done < <(find "$MODELS_SRC" -name "*.gguf" | sort)
    if [ -n "$found" ]; then
        ln -sf "$found" "$MODELS/$want"
        echo "  $want -> $found"
    else
        echo "  $want: NOT FOUND under $MODELS_SRC"
        MISSING=1
    fi
done
[ "$MISSING" = 0 ] || { echo "FATAL: missing models above" >&2; exit 1; }

if [ "$CALIBRATE" = 1 ]; then
    echo "=== [4/4] Calibrate (sanity gate) ==="
    cd "$REPO_ROOT"
    python3 "$SCRIPT_DIR/client.py" "$SCRIPT_DIR/smoke-real.json" \
        --ngl "$NGL" --sanity-only
else
    echo "=== [4/4] Calibrate SKIPPED (--no-calibrate) ==="
fi

echo ""
echo "=== Setup complete ==="
if [ "$LAUNCH" = 1 ]; then
    echo "launching autopilot (log: $REPO_ROOT/autopilot.out)"
    nohup python3 "$SCRIPT_DIR/autopilot.py" --ngl "$NGL" \
        > "$REPO_ROOT/autopilot.out" 2>&1 &
    echo "pid $! — progress: tail -f $REPO_ROOT/autopilot.out"
else
    echo "launch with:"
    echo "  source $VENV_DIR/bin/activate && nohup python3 $SCRIPT_DIR/autopilot.py --ngl $NGL > autopilot.out 2>&1 &"
fi
