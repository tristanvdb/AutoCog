#!/usr/bin/env bash
# Regenerate golden files from current tool output.
# Validates every regenerated golden against its schema before writing.
#
# Usage:
#   BUILD_DIR=build tests/update-golden.sh [--target sta|ir|all] [--dry-run]
#
# Requires: python3 with jsonschema, deepdiff, referencing
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
BUILD_DIR="${BUILD_DIR:-${PROJECT_DIR}/build}"
COMPARE="${SCRIPT_DIR}/golden_compare.py"
FIXTURES="${SCRIPT_DIR}/fixtures/stl"

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
skip=0

regenerate_sta() {
    local fixture="$1"
    local rel
    rel=$(python3 -c "import os; print(os.path.relpath('$fixture', '$FIXTURES'))")
    local rel_noext="${rel%.stl}"
    local golden_dir="${SCRIPT_DIR}/integration/tools/stlc/sta"
    local golden="${golden_dir}/${rel_noext}.golden.json"

    if [[ ! -f "$golden" ]]; then
        skip=$((skip + 1))
        return
    fi

    # Determine extra args
    local extra_args=()
    if [[ "$fixture" == *"/language/symbols/"* ]]; then
        extra_args+=("-I" "${FIXTURES}/language/symbols")
    fi

    local tmpfile
    tmpfile=$(mktemp)
    trap "rm -f '$tmpfile'" RETURN

    if ! "$STLC" --emit sta "${extra_args[@]}" "$fixture" > "$tmpfile" 2>/dev/null; then
        echo "REGEN FAILED: $rel (target=sta)"
        echo "  step: stlc execution"
        fail=$((fail + 1))
        return
    fi

    # Validate against schema
    if ! python3 "$COMPARE" --validate-only "$tmpfile" 2>/dev/null; then
        echo "REGEN FAILED: $rel (target=sta)"
        echo "  step: schema validation post-emit"
        python3 "$COMPARE" --validate-only "$tmpfile" 2>&1 | sed 's/^/  /'
        fail=$((fail + 1))
        return
    fi

    if $DRY_RUN; then
        # Compare only
        if ! python3 "$COMPARE" --no-validate "$tmpfile" "$golden" 2>/dev/null; then
            echo "WOULD UPDATE: $rel (target=sta)"
        fi
    else
        cp "$tmpfile" "$golden"
    fi
    pass=$((pass + 1))
}

regenerate_ir() {
    local fixture="$1"
    local rel
    rel=$(python3 -c "import os; print(os.path.relpath('$fixture', '$FIXTURES'))")
    local rel_noext="${rel%.stl}"
    local golden_dir="${SCRIPT_DIR}/integration/tools/stlc/ir"
    local golden="${golden_dir}/${rel_noext}.golden.json"

    if [[ ! -f "$golden" ]]; then
        skip=$((skip + 1))
        return
    fi

    local extra_args=()
    if [[ "$fixture" == *"/language/symbols/"* ]]; then
        extra_args+=("-I" "${FIXTURES}/language/symbols")
    fi

    local tmpfile
    tmpfile=$(mktemp)
    trap "rm -f '$tmpfile'" RETURN

    if ! "$STLC" --emit ir "${extra_args[@]}" "$fixture" > "$tmpfile" 2>/dev/null; then
        echo "REGEN FAILED: $rel (target=ir)"
        echo "  step: stlc execution"
        fail=$((fail + 1))
        return
    fi

    # Validate against schema
    if ! python3 "$COMPARE" --validate-only "$tmpfile" 2>/dev/null; then
        echo "REGEN FAILED: $rel (target=ir)"
        echo "  step: schema validation post-emit"
        python3 "$COMPARE" --validate-only "$tmpfile" 2>&1 | sed 's/^/  /'
        fail=$((fail + 1))
        return
    fi

    if $DRY_RUN; then
        if ! python3 "$COMPARE" --no-validate "$tmpfile" "$golden" 2>/dev/null; then
            echo "WOULD UPDATE: $rel (target=ir)"
        fi
    else
        cp "$tmpfile" "$golden"
    fi
    pass=$((pass + 1))
}

# Collect all STL fixtures (same logic as CMakeLists.txt)
while IFS= read -r -d '' stl; do
    name=$(basename "$stl")
    # Skip library files and error fixtures
    [[ "$name" == *"_lib"* ]] && continue
    [[ "$stl" == *"/errors/"* ]] && continue

    if [[ "$TARGET" == "all" || "$TARGET" == "sta" ]]; then
        regenerate_sta "$stl"
    fi
    if [[ "$TARGET" == "all" || "$TARGET" == "ir" ]]; then
        regenerate_ir "$stl"
    fi
done < <(find "$FIXTURES" -name "*.stl" -print0 | sort -z)

echo ""
echo "Regeneration complete: $pass updated, $fail failed, $skip skipped"
if [[ $fail -gt 0 ]]; then
    exit 1
fi
