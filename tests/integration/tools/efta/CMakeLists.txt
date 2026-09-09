# Frame -> FTT encoder round-trip tests: for every e2e golden frame and every
# shipped syntax S, encode(frame) under S then parse back with psta must
# reproduce the frame exactly:
#
#     stlc -> ista(S) -> efta --rng (golden frame) -> psta == golden frame
#
# The frame is syntax-independent, so the same golden must round-trip under
# all four syntaxes — that property is what makes a recorded trace
# re-renderable under syntaxes the generating model never saw (the special-
# token arc's load-bearing step). xfta is deliberately absent: the encoder
# replaces generation.

set(E2E_GOLDEN ${PROJECT_SOURCE_DIR}/tests/integration/tools/e2e/golden)
set(STL_FIXTURES ${PROJECT_SOURCE_DIR}/tests/fixtures/stl)
set(SEARCH ${PROJECT_SOURCE_DIR}/share/search/default.json)
set(STLIB ${PROJECT_SOURCE_DIR}/share/library/stlib)

file(GLOB_RECURSE GOLDEN_FILES "${E2E_GOLDEN}/*.golden.json")

# Quality-benchmark smoke: the v0.7.2 scorer pipeline (engine run with
# recorder -> batched efta --score -> accuracy/overhead/friction events)
# holds together end to end on the RNG model.
add_test(
    NAME quality_bench_smoke
    COMMAND ${CMAKE_COMMAND} -E env
        bash -c "
            set -e
            OUT=$(mktemp -d)
            ${Python_EXECUTABLE} ${PROJECT_SOURCE_DIR}/benchmarks/quality/run.py \
                --build ${CMAKE_BINARY_DIR} --rng --syntaxes default \
                --demos select --questions 1 --out \$OUT
            test -s \$OUT/results-*.ndjson
            ${Python_EXECUTABLE} -c \"
import glob, json
[e] = [json.loads(l) for l in open(glob.glob('\$OUT/results-*.ndjson')[0])]
assert e['autocog.bench.tokens.structure'] > 0, e
assert e['autocog.bench.friction.structure'] is not None, e
print('OK: quality scorer emits overhead + friction')
\"
            rm -rf \$OUT
        "
)
set_tests_properties(quality_bench_smoke PROPERTIES LABELS "efta;quality" TIMEOUT 120)

# Scored encode: with --score every non-root node's logprobs carry
# P(token | prefix) — the constraint-friction raw data. RNG smoke: values are
# random but must be present and per-token.
set(SC_STL ${STL_FIXTURES}/language/structures/test_basic_record.stl)
set(SC_GOLDEN ${E2E_GOLDEN}/language/structures/test_basic_record.test.golden.json)
add_test(
    NAME efta_score_smoke
    COMMAND ${CMAKE_COMMAND} -E env
        bash -c "
            set -e
            STA=$(mktemp) FTA=$(mktemp) FTT=$(mktemp)
            $<TARGET_FILE:autocog_stlc> --sta \$STA -I ${STLIB} ${SC_STL} 2>/dev/null
            $<TARGET_FILE:autocog_ista> --sta \$STA --prompt test --syntax ${PROJECT_SOURCE_DIR}/share/syntax/default.json --search ${SEARCH} --content {} --fta \$FTA 2>/dev/null
            $<TARGET_FILE:autocog_efta> --sta \$STA --fta \$FTA --prompt test --frame ${SC_GOLDEN} --rng --score --ftt \$FTT 2>/dev/null
            ${Python_EXECUTABLE} -c \"
import json, sys
d = json.load(open('\$FTT'))
def walk(n, depth):
    if depth > 0 and n['tokens'] and not n['logprobs']:
        print('unscored node:', n.get('uid')); sys.exit(1)
    if depth > 0 and len(n['logprobs']) != len(n['tokens']):
        print('logprob/token mismatch:', n.get('uid')); sys.exit(1)
    for c in n['children']: walk(c, depth + 1)
walk(d, 0)
print('OK: scored encode carries per-token logprobs')
\"
            rm -f \$STA \$FTA \$FTT
        "
)
set_tests_properties(efta_score_smoke PROPERTIES LABELS "efta;score" TIMEOUT 30)

foreach(syntax_name default chatml llama2chat plain special)
  set(SYNTAX ${PROJECT_SOURCE_DIR}/share/syntax/${syntax_name}.json)
  foreach(golden_path ${GOLDEN_FILES})
    file(RELATIVE_PATH golden_rel "${E2E_GOLDEN}" "${golden_path}")
    get_filename_component(golden_dir "${golden_rel}" DIRECTORY)
    get_filename_component(golden_file "${golden_rel}" NAME_WLE)
    string(REGEX REPLACE "\\.golden$" "" base_prompt "${golden_file}")
    string(REGEX MATCH "^(.+)\\.([^.]+)$" _ "${base_prompt}")
    set(test_name "${CMAKE_MATCH_1}")
    set(prompt_name "${CMAKE_MATCH_2}")
    set(stl_path "${STL_FIXTURES}/${golden_dir}/${test_name}.stl")
    string(REPLACE "/" "_" test_label "${golden_dir}/${test_name}.${prompt_name}.${syntax_name}")

    add_test(
        NAME "enc_${test_label}"
        COMMAND ${CMAKE_COMMAND} -E env
            bash -c "
                set -e
                STA=$(mktemp) FTA=$(mktemp) FTT=$(mktemp) RESULT=$(mktemp)
                STL_DIR=$(dirname ${stl_path})
                $<TARGET_FILE:autocog_stlc> --sta \$STA -I ${STLIB} -I \$STL_DIR ${stl_path} 2>/dev/null
                $<TARGET_FILE:autocog_ista> --sta \$STA --prompt ${prompt_name} --syntax ${SYNTAX} --search ${SEARCH} --content {} --fta \$FTA 2>/dev/null
                $<TARGET_FILE:autocog_efta> --sta \$STA --fta \$FTA --prompt ${prompt_name} --frame ${golden_path} --rng --ftt \$FTT 2>/dev/null
                $<TARGET_FILE:autocog_psta> --sta \$STA --ftt \$FTT --prompt ${prompt_name} --frame \$RESULT 2>/dev/null
                ${Python_EXECUTABLE} -c \"
import json, sys
with open('\$RESULT') as f: got = json.load(f)
with open('${golden_path}') as f: want = json.load(f)
if got != want:
    print('ROUND-TRIP MISMATCH (${syntax_name})')
    print('got: ', json.dumps(got, indent=2)[:300])
    print('want:', json.dumps(want, indent=2)[:300])
    sys.exit(1)
\"
                rm -f \$STA \$FTA \$FTT \$RESULT
            "
    )
    set_tests_properties("enc_${test_label}" PROPERTIES
        LABELS "efta;roundtrip"
        TIMEOUT 30
    )
  endforeach()
endforeach()
