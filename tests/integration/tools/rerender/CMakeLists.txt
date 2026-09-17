# Re-rendering a recorded frame: for every e2e golden frame and every shipped
# syntax S, instantiating the prompt with the frame AS CONTENT must produce a
# single forced path that parses back to the frame exactly:
#
#     stlc -> ista(S, content=frame) -> xfta --rng -> psta == frame
#
# A frame is syntax-independent, so the same golden must round-trip under all
# eight syntaxes — that property is what makes a recorded trace re-renderable
# under syntaxes the generating model never saw (the special-token arc's
# load-bearing step).
#
# There is no separate encoder. A complete frame supplied as content leaves the
# instantiator nothing to ask the model: every field takes its value, every
# branch takes the pre-filled successor, and the FTA collapses to one chain of
# text actions. Re-rendering is therefore instantiation, and cannot drift from
# the renderer generation uses — which is exactly what a hand-written inverse
# (the former `efta`) was free to do.

set(E2E_GOLDEN ${PROJECT_SOURCE_DIR}/tests/integration/tools/e2e/golden)
set(STL_FIXTURES ${PROJECT_SOURCE_DIR}/tests/fixtures/stl)
set(SEARCH ${PROJECT_SOURCE_DIR}/share/search/default.json)
set(STLIB ${PROJECT_SOURCE_DIR}/share/library/stlib)

file(GLOB_RECURSE GOLDEN_FILES "${E2E_GOLDEN}/*.golden.json")

set(SC_STL ${STL_FIXTURES}/language/structures/test_basic_record.stl)
set(SC_GOLDEN ${E2E_GOLDEN}/language/structures/test_basic_record.test.golden.json)

# --score-all: every non-root node's logprobs carry P(token | prefix) — the
# constraint-friction raw data. RNG smoke: the values are random, but they must
# be present, per-token, and not all zero. The root is the unconditioned prompt
# and keeps its zeros, so the assertion starts below it.
add_test(
    NAME rerender_score_all
    COMMAND ${CMAKE_COMMAND} -E env
        bash -c "
            set -e
            STA=$(mktemp) FTA=$(mktemp) FTT=$(mktemp)
            $<TARGET_FILE:autocog_stlc> --sta \$STA -I ${STLIB} ${SC_STL} 2>/dev/null
            $<TARGET_FILE:autocog_ista> --sta \$STA --prompt test --syntax ${PROJECT_SOURCE_DIR}/share/syntax/complete.json --search ${SEARCH} --content ${SC_GOLDEN} --fta \$FTA 2>/dev/null
            $<TARGET_FILE:autocog_xfta> --fta \$FTA --rng --score-all --ftt \$FTT 2>/dev/null
            ${Python_EXECUTABLE} -c \"
import json, sys
scored = []
def walk(n, depth):
    if depth > 0:
        if n['tokens'] and not n['logprobs']:
            print('unscored node:', n.get('uid')); sys.exit(1)
        if len(n['logprobs']) != len(n['tokens']):
            print('logprob/token mismatch:', n.get('uid')); sys.exit(1)
        scored.extend(n['logprobs'])
    for c in n['children']: walk(c, depth + 1)
walk(json.load(open('\$FTT')), 0)
if not scored:
    print('nothing below the root was scored'); sys.exit(1)
if all(v == 0.0 for v in scored):
    print('every logprob is zero: --score-all did not arm'); sys.exit(1)
print('OK: --score-all carries per-token logprobs')
\"
            rm -f \$STA \$FTA \$FTT
        "
)
set_tests_properties(rerender_score_all PROPERTIES LABELS "rerender;score" TIMEOUT 30)

# Without --score-all the same path must come back unscored. Fixed text is a
# prefix the model is going to be conditioned on anyway; decoding it to learn a
# logprob nobody reads is pure cost. This is the guard that generation does not
# quietly start paying the scoring bill.
add_test(
    NAME rerender_no_score_by_default
    COMMAND ${CMAKE_COMMAND} -E env
        bash -c "
            set -e
            STA=$(mktemp) FTA=$(mktemp) FTT=$(mktemp)
            $<TARGET_FILE:autocog_stlc> --sta \$STA -I ${STLIB} ${SC_STL} 2>/dev/null
            $<TARGET_FILE:autocog_ista> --sta \$STA --prompt test --syntax ${PROJECT_SOURCE_DIR}/share/syntax/complete.json --search ${SEARCH} --content ${SC_GOLDEN} --fta \$FTA 2>/dev/null
            $<TARGET_FILE:autocog_xfta> --fta \$FTA --rng --ftt \$FTT 2>/dev/null
            ${Python_EXECUTABLE} -c \"
import json, sys
def walk(n):
    if any(v != 0.0 for v in n['logprobs']):
        print('scored without --score-all:', n.get('uid')); sys.exit(1)
    for c in n['children']: walk(c)
walk(json.load(open('\$FTT')))
print('OK: text actions stay free unless asked for')
\"
            rm -f \$STA \$FTA \$FTT
        "
)
set_tests_properties(rerender_no_score_by_default PROPERTIES LABELS "rerender;score" TIMEOUT 30)

foreach(syntax_name complete indent-index indent stripped chatml llama2chat llama3chat special)
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
        NAME "rerender_${test_label}"
        COMMAND ${CMAKE_COMMAND} -E env
            bash -c "
                set -e
                STA=$(mktemp) FTA=$(mktemp) FTT=$(mktemp) RESULT=$(mktemp)
                STL_DIR=$(dirname ${stl_path})
                $<TARGET_FILE:autocog_stlc> --sta \$STA -I ${STLIB} -I \$STL_DIR ${stl_path} 2>/dev/null
                $<TARGET_FILE:autocog_ista> --sta \$STA --prompt ${prompt_name} --syntax ${SYNTAX} --search ${SEARCH} --content ${golden_path} --fta \$FTA 2>/dev/null
                $<TARGET_FILE:autocog_xfta> --fta \$FTA --rng --ftt \$FTT 2>/dev/null
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
    set_tests_properties("rerender_${test_label}" PROPERTIES
        LABELS "rerender;roundtrip"
        TIMEOUT 30
    )
  endforeach()
endforeach()
