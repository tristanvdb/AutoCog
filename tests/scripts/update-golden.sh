#!/usr/bin/env bash
# Regenerate golden files from current tool output.
# Validates every regenerated golden against its schema before writing.
#
# Usage:
#   BUILD_DIR=build tests/scripts/update-golden.sh [--target sta|ir|e2e|all] [--dry-run]
#
# Targets:
#   sta / ir   per-fixture compiler goldens (tests/integration/tools/stlc/<target>/)
#   e2e        full-pipeline frame goldens (tests/integration/tools/e2e/), regenerated
#              by running stlc -> ista -> xfta --rng --seed 42 -> psta and validating
#              the intermediate FTA and FTT against their schemas
#   all        every target above
#
# Note: the stlc emit-target tests (tests/integration/tools/stlc/emit/) are
# assertion-based, not golden-diffed, so there is nothing to regenerate there.
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

    if ! "$STLC" "--$target" "$tmpfile" "${extra_args[@]}" "$fixture" 2>/dev/null; then
        echo "REGEN FAILED: $rel (target=$target)"
        echo "  step: stlc execution"
        fail=$((fail + 1)); return
    fi

    # Validate against schema, capturing stderr once and printing it only on
    # failure (--force-validate so a missing jsonschema is a hard error here too).
    local validate_err
    if ! validate_err=$(python3 "$COMPARE" --force-validate --format "$target" --validate-only "$tmpfile" 2>&1); then
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
        # Normalize the non-deterministic / build-specific metadata before
        # storing: timestamp -> fixed sentinel, version -> 0.0.0 placeholder.
        # golden_compare.py excludes both from its structural diff, so the golden
        # does not churn when the wall clock or the project version changes. The
        # rest of the metadata block (format, hash) and any provenance are part of
        # the artifact's identity and are persisted. Sentinels (rather than
        # dropping the fields) keep the golden schema-valid, since both are
        # required by the metadata schema.
        python3 -c "import json,sys
d=json.load(open(sys.argv[1]))
m=d.get('metadata')
if isinstance(m, dict):
    if 'timestamp' in m: m['timestamp'] = '1970-01-01T00:00:00Z'
    if 'version' in m:   m['version']   = '0.0.0'
json.dump(d, open(sys.argv[2],'w'), indent=2)
open(sys.argv[2],'a').write('\n')" "$tmpfile" "$golden"
    fi
    pass=$((pass + 1))
}

# --- e2e full-pipeline goldens --------------------------------------------
SYNTAX="${PROJECT_DIR}/share/syntax/default.json"
SEARCH="${PROJECT_DIR}/share/search/default.json"
STLIB="${PROJECT_DIR}/share/library/stlib"
ISTA="${BUILD_DIR}/tools/ista/ista"
XFTA="${BUILD_DIR}/tools/xfta/xfta"
PSTA="${BUILD_DIR}/tools/psta/psta"
E2E_DIR="${TESTS_DIR}/integration/tools/e2e"

# Run stlc -> ista -> xfta --rng --seed 42 -> psta for one (fixture, prompt),
# validating the FTA and FTT against their schemas, and write the psta frame to
# the golden. Args: stl prompt golden [content].
regenerate_e2e_one() {
    local stl="$1" prompt="$2" golden="$3" content="${4:-}"
    local rel
    rel=$(python3 -c "import os; print(os.path.relpath('$golden', '$TESTS_DIR'))")

    local sta fta ftt result
    sta=$(mktemp); fta=$(mktemp); ftt=$(mktemp); result=$(mktemp)
    trap "rm -f '$sta' '$fta' '$ftt' '$result'" RETURN

    local stl_dir; stl_dir=$(dirname "$stl")
    if ! "$STLC" --sta "$sta" -I "$STLIB" -I "$stl_dir" "$stl" 2>/dev/null; then
        echo "REGEN FAILED: $rel — step: stlc"; fail=$((fail + 1)); return
    fi

    # --content is required; default to an empty object when none was supplied.
    local content_val="$content"
    [[ -z "$content_val" ]] && content_val='{}'
    if ! "$ISTA" --sta "$sta" --prompt "$prompt" --syntax "$SYNTAX" --search "$SEARCH" \
                 --content "$content_val" --fta "$fta" 2>/dev/null; then
        echo "REGEN FAILED: $rel — step: ista"; fail=$((fail + 1)); return
    fi
    local verr
    if ! verr=$(python3 "$COMPARE" --force-validate --validate-only "$fta" 2>&1); then
        echo "REGEN FAILED: $rel — step: FTA schema validation"
        echo "$verr" | sed 's/^/  /'; fail=$((fail + 1)); return
    fi

    if ! "$XFTA" --rng --seed 42 --fta "$fta" --ftt "$ftt" 2>/dev/null; then
        echo "REGEN FAILED: $rel — step: xfta"; fail=$((fail + 1)); return
    fi
    if ! verr=$(python3 "$COMPARE" --force-validate --format ftt --validate-only "$ftt" 2>&1); then
        echo "REGEN FAILED: $rel — step: FTT schema validation"
        echo "$verr" | sed 's/^/  /'; fail=$((fail + 1)); return
    fi

    if ! "$PSTA" --sta "$sta" --ftt "$ftt" --prompt "$prompt" --frame "$result" 2>/dev/null; then
        echo "REGEN FAILED: $rel — step: psta"; fail=$((fail + 1)); return
    fi

    if $DRY_RUN; then
        if ! python3 "$COMPARE" --no-validate "$result" "$golden" >/dev/null 2>&1; then
            echo "WOULD UPDATE: $rel"
        fi
    else
        [[ -f "$golden" ]] || { echo "NEW: $rel — creating golden"; mkdir -p "$(dirname "$golden")"; }
        cp "$result" "$golden"
    fi
    pass=$((pass + 1))
}

# Regenerate every e2e golden, mirroring tests/integration/tools/e2e/CMakeLists.txt.
regenerate_e2e() {
    if [[ ! -x "$ISTA" || ! -x "$XFTA" || ! -x "$PSTA" ]]; then
        echo "Error: ista/xfta/psta not found under $BUILD_DIR/tools" >&2
        echo "Build the tools before regenerating e2e goldens." >&2
        fail=$((fail + 1)); return
    fi

    # Shape 1: golden/<category>/<test_name>.<prompt>.golden.json
    local golden rel category base prompt test_name
    while IFS= read -r -d '' golden; do
        rel="${golden#${E2E_DIR}/golden/}"        # category/test.prompt.golden.json
        category=$(dirname "$rel")
        base=$(basename "$rel" .golden.json)       # test.prompt
        prompt="${base##*.}"
        test_name="${base%.*}"
        regenerate_e2e_one "${FIXTURES}/${category}/${test_name}.stl" "$prompt" "$golden"
    done < <(find "${E2E_DIR}/golden" -name '*.golden.json' -print0 | sort -z)

    # Shape 2: pre-filled deep-nesting round-trip (prompt "test", -d content)
    regenerate_e2e_one \
        "${FIXTURES}/language/structures/test_deep_nesting.stl" test \
        "${E2E_DIR}/prefilled/test_deep_nesting.golden.json" \
        "${E2E_DIR}/test_deep_nesting_content.json"

    # Shape 3: variable-length arrays (prompt "test", -d <variant>.content.json)
    local variant
    for variant in min max mid mixed asym; do
        regenerate_e2e_one \
            "${FIXTURES}/language/structures/test_variable_arrays.stl" test \
            "${E2E_DIR}/variable/${variant}.golden.json" \
            "${E2E_DIR}/variable/${variant}.content.json"
    done
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

if [[ "$TARGET" == "all" || "$TARGET" == "e2e" ]]; then
    regenerate_e2e
fi

echo ""
echo "Regeneration complete: $pass updated, $fail failed"
if [[ $fail -gt 0 ]]; then
    exit 1
fi
