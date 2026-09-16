#!/usr/bin/env bash
# Fetch everything a campaign needs: models into models/, accuracy datasets
# into datasets/ — both alongside the repo (see env.sh; NFS-persistent, so
# each download happens once). Run it directly, or let setup.sh call it.
#
#     autocog/share/experiments/downloader.sh [--datasets-only] [descriptor.json ...]
#
#   --datasets-only   skip the models (e.g. gguf already in place)
#   descriptors       campaign descriptors: fetch only the models they
#                     name (minimal set) instead of the MODEL_SIZE tier
#   MODEL_SIZE={0,1,2}  scale tier (see models.sh): 0 = tiny + 1B pair
#                       (default), 1 adds the 3B/8B pairs, 2 adds 14B/32B
#
# Everything is skip-if-present, so pre-placing archives by hand works too.
# Datasets are the canonical no-auth distributions:
#   ARC (Easy + Challenge)  https://ai2-public-datasets.s3.amazonaws.com/arc/ARC-V1-Feb2018.zip
#   MMLU (Hendrycks et al.) https://people.eecs.berkeley.edu/~hendrycks/data.tar
set -euo pipefail

EXP_DIR="$(cd "$(dirname "$0")" && pwd)"
# shellcheck disable=SC1091
source "$EXP_DIR/env.sh"

DATASETS_ONLY=0
DESCRIPTORS=()
for arg in "$@"; do
    case "$arg" in
        --datasets-only) DATASETS_ONLY=1 ;;
        -*) echo "usage: $0 [--datasets-only] [descriptor.json ...]" >&2; exit 1 ;;
        *) DESCRIPTORS+=("$(realpath "$arg")") ;;  # models.sh cds away
    esac
done

if [ "$DATASETS_ONLY" -eq 0 ]; then
    echo "=== models into $MODELS_DIR ==="
    "$EXP_DIR/models.sh" "${DESCRIPTORS[@]}"
fi

echo "=== datasets into $DATASETS_DIR ==="
mkdir -p "$DATASETS_DIR"
cd "$DATASETS_DIR"

fetch() {  # fetch FILE URL
    [ -s "$1" ] && { echo "have: $1"; return 0; }
    echo "fetching: $1"
    curl -L --fail --retry 3 -o "$1.part" "$2"
    mv "$1.part" "$1"
}

# ARC: the zip unpacks to a versioned top dir (observed: ARC-V1-Feb2018-2/)
# with ARC-Easy/ARC-Challenge train/dev/test JSONL (plus the ARC corpus and
# __MACOSX junk, which we ignore) — glob, don't assume the exact name.
if ! ls ARC-V1-Feb2018*/ARC-Easy/ARC-Easy-Test.jsonl > /dev/null 2>&1; then
    fetch ARC-V1-Feb2018.zip "https://ai2-public-datasets.s3.amazonaws.com/arc/ARC-V1-Feb2018.zip"
    unzip -q -o ARC-V1-Feb2018.zip -x "__MACOSX/*"
else
    echo "have: $(ls -d ARC-V1-Feb2018*/ | head -1)"
fi

# MMLU: data.tar unpacks to data/{test,val,dev,auxiliary_train}; keep it
# under mmlu/ so the directory says what it is.
if [ ! -d mmlu/test ]; then
    fetch mmlu-data.tar "https://people.eecs.berkeley.edu/~hendrycks/data.tar"
    mkdir -p mmlu
    tar -xf mmlu-data.tar -C mmlu --strip-components=1
else
    echo "have: mmlu/"
fi

# Campaign question files (the names campaign datasets refer to), converted
# beside the raw distributions; skip-if-present like everything else.
CONVERT="$REPO/share/benchmarks/quality/convert.py"
EASY=$(ls ARC-V1-Feb2018*/ARC-Easy/ARC-Easy-Test.jsonl 2>/dev/null | head -1)
CHAL=$(ls ARC-V1-Feb2018*/ARC-Challenge/ARC-Challenge-Test.jsonl 2>/dev/null | head -1)
[ -s arc-easy.json ]      || python3 "$CONVERT" arc "$EASY" --out arc-easy.json
[ -s arc-challenge.json ] || python3 "$CONVERT" arc "$CHAL" --out arc-challenge.json
[ -s arc-challenge-500.json ] || python3 "$CONVERT" arc "$CHAL" --limit 500 --out arc-challenge-500.json

# Whatever else the descriptors name: <source>[-<limit>], converted from the
# raw distributions above. A campaign asking for "mmlu-1000" or
# "arc-challenge-250" gets it here rather than by hand.
if [ "${#DESCRIPTORS[@]}" -gt 0 ]; then
    WANTED=$(python3 -c "
import json, sys
names = []
for p in sys.argv[1:]:
    names += json.load(open(p)).get('datasets', [])
print('\n'.join(sorted(set(names))))" "${DESCRIPTORS[@]}")
    while IFS= read -r NAME; do
        [ -n "$NAME" ] || continue
        [ -s "$NAME.json" ] && { echo "have: $NAME.json"; continue; }
        LIMIT="${NAME##*-}"
        case "$NAME" in
            mmlu|mmlu-*)
                ARGS=(mmlu "$DATASETS_DIR/mmlu/test") ;;
            arc-easy-*)
                ARGS=(arc "$EASY") ;;
            arc-challenge-*)
                ARGS=(arc "$CHAL") ;;
            *)
                echo "WARNING: dataset '$NAME' named by a descriptor is not" \
                     "a known distribution and does not exist — the campaign" \
                     "will refuse to start" >&2
                continue ;;
        esac
        if [ "$LIMIT" -eq "$LIMIT" ] 2>/dev/null; then
            ARGS+=(--limit "$LIMIT")
        fi
        echo "converting: $NAME.json"
        python3 "$CONVERT" "${ARGS[@]}" --out "$NAME.json"
    done <<< "$WANTED"
fi

echo
echo "datasets ready:"
ls -d "$DATASETS_DIR"/ARC-V1-Feb2018*/ "$DATASETS_DIR"/mmlu 2>/dev/null || true
ls "$DATASETS_DIR"/arc-*.json "$DATASETS_DIR"/mmlu-*.json 2>/dev/null || true
