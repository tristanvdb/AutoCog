#!/usr/bin/env bash
#
# Run a campaign. The only entry point.
#
#     cd ~/my-nfs && autocog/share/experiments/campaign.sh my-campaign.yml
#
# Setup is part of this: the venv, the build, the model and dataset downloads
# all happen here and all are idempotent, so there is nothing to run first and
# nothing to remember the order of. Artifacts land beside the checkout
# (see share/bootstrap.sh); results go to results/<descriptor basename>.
#
# Everything past the bootstrap is `python3 -m campaign` — see
# share/experiments/campaign/. The harness is a separate application: it is
# a CLIENT of autocog, not part of it, and carries its own tests
# (share/experiments/tests).

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck disable=SC1091
source "$SCRIPT_DIR/../bootstrap.sh"

PYTHONPATH="$SCRIPT_DIR${PYTHONPATH:+:$PYTHONPATH}" exec python3 -m campaign "$@"
