#!/usr/bin/env bash
# Fetch everything a campaign needs: models into models/, accuracy datasets
# into datasets/ — both alongside the repo (see env.sh; NFS-persistent, so
# each download happens once). Run it directly, or let setup.sh call it.
#
#     autocog/experiments/downloader.sh [--datasets-only]
#
#   --datasets-only   skip the models (e.g. gguf already in place)
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
case "${1:-}" in
    --datasets-only) DATASETS_ONLY=1 ;;
    "") ;;
    *) echo "usage: $0 [--datasets-only]" >&2; exit 1 ;;
esac

if [ "$DATASETS_ONLY" -eq 0 ]; then
    echo "=== models into $MODELS_DIR ==="
    "$EXP_DIR/models.sh"
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

echo
echo "datasets ready:"
ls -d "$DATASETS_DIR"/ARC-V1-Feb2018*/ "$DATASETS_DIR"/mmlu 2>/dev/null || true
