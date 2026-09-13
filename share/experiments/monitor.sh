#!/usr/bin/env bash
# Campaign status screen — open in a second terminal while campaign.sh runs:
#     cd ~/my-nfs && autocog/share/experiments/monitor.sh [--once]
set -euo pipefail
# shellcheck disable=SC1091
source "$(dirname "$0")/env.sh"
[ -x "$VENV/bin/python3" ] && source "$VENV/bin/activate"
exec python3 "$REPO/share/benchmarks/monitor.py" \
    "${RESULTS_PATH:-$RESULTS_DIR}/campaign-events.ndjson" "$@"
