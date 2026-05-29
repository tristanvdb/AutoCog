#!/bin/bash
# Fuzz an STL program with random RNG seeds.
#
# Usage:
#   tests/scripts/fuzz.sh [OPTIONS] --stl PROGRAM.stl --input DATA
#
# Options:
#   --stl PATH        STL source file (required)
#   --input DATA      Input JSON (file path or inline, required)
#   --entry NAME      Entry point (default: main)
#   -I PATH           Include path (repeatable)
#   -n COUNT          Number of seeds to try (default: 50)
#   --max-steps N     Max prompt steps per run (default: 100)
#   -v                Show each seed's output on failure
#
# Example:
#   tests/scripts/fuzz.sh --stl share/demos/story-writer/writer.stl \
#                 --input '{"query":"bedtime story","age":"3"}' \
#                 -I share/library -I share/demos/story-writer \
#                 -n 100

set -u

STL=""
INPUT=""
ENTRY="main"
COUNT=50
MAX_STEPS=100
VERBOSE=false
INCLUDES=()

while [[ $# -gt 0 ]]; do
    case "$1" in
        --stl)      STL="$2"; shift 2 ;;
        --input)    INPUT="$2"; shift 2 ;;
        --entry)    ENTRY="$2"; shift 2 ;;
        -I)         INCLUDES+=(-I "$2"); shift 2 ;;
        -n)         COUNT="$2"; shift 2 ;;
        --max-steps) MAX_STEPS="$2"; shift 2 ;;
        -v)         VERBOSE=true; shift ;;
        -h|--help)
            sed -n '2,/^$/s/^# \?//p' "$0"
            exit 0 ;;
        *)
            echo "Unknown option: $1" >&2
            exit 1 ;;
    esac
done

if [[ -z "$STL" || -z "$INPUT" ]]; then
    echo "Error: --stl and --input are required" >&2
    exit 1
fi

# Generate random seeds
SEEDS=$(python3 -c "
import random, sys
random.seed()
seeds = sorted(random.sample(range(1, 1000000), $COUNT))
print(' '.join(str(s) for s in seeds))
")

pass=0
fail=0
error=0
errors=""

echo "Fuzzing: $(basename "$STL") ($COUNT seeds, max_steps=$MAX_STEPS)"
echo "Input: $INPUT"
echo ""

for seed in $SEEDS; do
    # Run in --json mode and capture the NDJSON log stream (stderr). Data goes
    # to stdout (discarded here); logs/errors are structured on stderr.
    output=$(python3 -m autocog --json run \
        --stl "$STL" "${INCLUDES[@]}" \
        --rng --seed "$seed" \
        --input "$INPUT" \
        --entry "$ENTRY" \
        --max-steps "$MAX_STEPS" 2>&1 >/dev/null)
    rc=$?

    if [[ $rc -eq 0 ]]; then
        pass=$((pass + 1))
        printf "."
        continue
    fi

    # Classify the failure on the structured error.type rather than substring
    # matching the message. An OrchestrationError is the expected outcome with
    # random tokens (recoverable); anything else -- a bare Python type, an
    # InternalError (event.kind "alert"), etc. -- is an unexpected error.
    # Parse with python3 (already a hard dependency) so the harness needs no jq.
    etype=$(printf '%s' "$output" | python3 -c "
import sys, json
etype = ''
for line in sys.stdin:
    line = line.strip()
    if not line:
        continue
    try:
        rec = json.loads(line)
    except ValueError:
        continue
    if rec.get('error.type'):
        etype = rec['error.type']  # last error record wins
print(etype)
")

    if [[ "$etype" == "OrchestrationError" ]]; then
        fail=$((fail + 1))
        printf "o"
        if $VERBOSE; then
            errors="${errors}\n--- seed=$seed (orchestration) ---\n${output}\n"
        fi
    else
        error=$((error + 1))
        printf "X"
        label="${etype:-unclassified}"
        errors="${errors}\n--- seed=$seed (rc=$rc, error.type=${label}) ---\n${output}\n"
    fi
done

echo ""
echo ""
echo "Results: $pass passed, $fail orchestration errors, $error unexpected errors (out of $COUNT)"

if [[ $error -gt 0 ]]; then
    echo ""
    echo "=== UNEXPECTED ERRORS ==="
    echo -e "$errors"
    exit 1
elif [[ $fail -gt 0 && $VERBOSE ]]; then
    echo ""
    echo "=== ORCHESTRATION ERRORS ==="
    echo -e "$errors"
fi
