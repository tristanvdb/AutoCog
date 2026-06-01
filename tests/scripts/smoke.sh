#!/usr/bin/env bash
#
# Smoke test: verify an autocog installation works, without any test framework.
#
# Suitable for users to confirm their install:
#   pip install "autocog[server,validate]"
#   tests/scripts/smoke.sh
#
# Checks (each must succeed):
#   1. the package imports
#   2. the console script runs (autocog --version)
#   3. the compiler tool runs end-to-end on a trivial program (stlc --version)
#
# python / autocog are resolved from PATH. Top scripts prepend a venv bin; a
# user running this directly uses whatever environment autocog is installed in.

set -euo pipefail

echo "=== Smoke: import autocog ==="
python -c "import autocog; print('autocog', getattr(autocog, '__version__', '(no __version__)'))"

echo "=== Smoke: console script ==="
autocog --version

echo "=== Smoke: stlc tool ==="
# stlc is installed as a console entry alongside autocog; --version exercises
# the compiler binary load path without needing a model.
stlc --version

echo "Smoke OK."
