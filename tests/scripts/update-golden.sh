#!/usr/bin/env bash
# Regenerate golden files from current tool output.
# Validates every regenerated golden against its schema before writing.
#
# Usage:
#   BUILD_DIR=build tests/scripts/update-golden.sh [--target sta|ir|all] [--dry-run]
#
# Requires: python3 with jsonschema, deepdiff, referencing
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
TESTS_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
PROJECT_DIR="$(cd "$TESTS_DIR/.." && pwd)"
BUILD_DIR="${BUILD_DIR:-${PROJECT_DIR}/build}"
COMPARE="${SCRIPT_DIR}/golden_compare.py"
FIXTURES="${TESTS_DIR}/fixtures/stl"

STLC="${BUILD_DIR}/tools/stlc/stlc"

TARGET="all"
DRY_RUN=false

while [[ $# -gt 0 ]]; do
    case "$1" in
        --target) TARGET="$2"; shift 2 ;;
        --dry-run) DRY_RUN=true; shift ;;
        -h|--help)
            sed -n '2,/^$/s/^# \?//p' "$0"
            exit 0 ;;
        *) echo "Unknown option: $1" >&2; exit 1 ;;
    esac
done

if [[ ! -x "$STLC" ]]; then
    echo "Error: stlc not found at $STLC" >&2
    echo "Set BUILD_DIR to your build directory" >&2
    exit 1
fi

pass=0
fail=0

# Regenerate one golden (target is "sta" or "ir") from a fixture's stlc output.
regenerate_one() {
    local target="$1"
    local fixture="$2"
    local rel
    rel=$(python3 -c "import os; print(os.path.relpath('$fixture', '$FIXTURES'))")
    local rel_noext="${rel%.stl}"
    local golden="${TESTS_DIR}/integration/tools/stlc/${target}/${rel_noext}.golden.json"

    # Bootstrap missing goldens rather than skipping: this script's purpose is
    # to generate them, and the dev reviews the resulting git diff. Create the
    # parent directory so the final cp succeeds.
    if [[ ! -f "$golden" ]]; then
        echo "NEW: $rel (target=$target) — creating golden"
        mkdir -p "$(dirname "$golden")"
    fi

    # Per-fixture compiler-flag special cases. Currently only language/symbols/
    # needs -I import resolution; when other categories need flags, consider a
    # per-directory flags file rather than extending this check.
    local extra_args=()
    if [[ "$fixture" == *"/language/symbols/"* ]]; then
        extra_args+=("-I" "${FIXTURES}/language/symbols")
    fi

    local tmpfile
    tmpfile=$(mktemp)
    trap "rm -f '$tmpfile'" RETURN

    if ! "$STLC" --emit "$target" "${extra_args[@]}" "$fixture" > "$tmpfile" 2>/dev/null; then
        echo "REGEN FAILED: $rel (target=$target)"
        echo "  step: stlc execution"
        fail=$((fail + 1)); return
    fi

    # Validate against schema, capturing stderr once and printing it only on
    # failure (--force-validate so a missing jsonschema is a hard error here too).
    local validate_err
    if ! validate_err=$(python3 "$COMPARE" --force-validate --validate-only "$tmpfile" 2>&1); then
        echo "REGEN FAILED: $rel (target=$target)"
        echo "  step: schema validation post-emit"
        echo "$validate_err" | sed 's/^/  /'
        fail=$((fail + 1)); return
    fi

    if $DRY_RUN; then
        if ! python3 "$COMPARE" --no-validate "$tmpfile" "$golden" 2>/dev/null; then
            echo "WOULD UPDATE: $rel (target=$target)"
        fi
    else
        # Strip the metadata block before storing. golden_compare.py already
        # excludes metadata from comparison (it changes every run -- timestamp,
        # producing version), so persisting it only causes spurious churn. The
        # stored golden is the metadata-free canonical payload; schema
        # validation above ran on the full emit, so nothing is lost.
        python3 -c "import json,sys
d=json.load(open(sys.argv[1]))
d.pop('metadata', None)
json.dump(d, open(sys.argv[2],'w'), indent=2)
open(sys.argv[2],'a').write('\n')" "$tmpfile" "$golden"
    fi
    pass=$((pass + 1))
}

# Collect all STL fixtures (same logic as CMakeLists.txt)
while IFS= read -r -d '' stl; do
    name=$(basename "$stl")
    # Skip library files and error fixtures
    [[ "$name" == *"_lib"* ]] && continue
    [[ "$stl" == *"/errors/"* ]] && continue

    if [[ "$TARGET" == "all" || "$TARGET" == "sta" ]]; then regenerate_one sta "$stl"; fi
    if [[ "$TARGET" == "all" || "$TARGET" == "ir" ]]; then regenerate_one ir "$stl"; fi
done < <(find "$FIXTURES" -name "*.stl" -print0 | sort -z)

echo ""
echo "Regeneration complete: $pass updated, $fail failed"
if [[ $fail -gt 0 ]]; then
    exit 1
fi
