# End-to-end pipeline tests: stlc → ista → xfta --rng → psta
# Each test compiles an STL fixture, instantiates a prompt, evaluates with RNG,
# parses the FTT, and compares against a golden output.

set(E2E_GOLDEN ${CMAKE_CURRENT_SOURCE_DIR}/golden)
set(STL_FIXTURES ${PROJECT_SOURCE_DIR}/tests/fixtures/stl)
set(SYNTAX ${PROJECT_SOURCE_DIR}/share/syntax/default.json)
set(SEARCH ${PROJECT_SOURCE_DIR}/share/search/default.json)
set(STLIB ${PROJECT_SOURCE_DIR}/share/library/stlib)

# Find all golden files
file(GLOB_RECURSE GOLDEN_FILES "${E2E_GOLDEN}/*.golden.json")

foreach(golden_path ${GOLDEN_FILES})
    # Extract relative path: golden/category/test.prompt.golden.json
    file(RELATIVE_PATH golden_rel "${E2E_GOLDEN}" "${golden_path}")

    # Parse: category/test_name.prompt_name.golden.json
    get_filename_component(golden_dir "${golden_rel}" DIRECTORY)    # category/subcategory
    get_filename_component(golden_file "${golden_rel}" NAME_WLE)    # test_name.prompt_name.golden
    # Remove .golden suffix
    string(REGEX REPLACE "\\.golden$" "" base_prompt "${golden_file}")
    # Split on last dot: test_name.prompt_name
    string(REGEX MATCH "^(.+)\\.([^.]+)$" _ "${base_prompt}")
    set(test_name "${CMAKE_MATCH_1}")
    set(prompt_name "${CMAKE_MATCH_2}")

    # STL source path
    set(stl_path "${STL_FIXTURES}/${golden_dir}/${test_name}.stl")

    # Test name for ctest
    string(REPLACE "/" "_" test_label "${golden_dir}/${test_name}.${prompt_name}")

    add_test(
        NAME "e2e_${test_label}"
        COMMAND ${CMAKE_COMMAND} -E env
            bash -c "
                set -e
                STA=$(mktemp) FTA_OUT=$(mktemp) RESULT=$(mktemp)
                STL_DIR=$(dirname ${stl_path})
                $<TARGET_FILE:autocog_stlc> --emit sta -I ${STLIB} -I \$STL_DIR ${stl_path} > \$STA 2>/dev/null
                $<TARGET_FILE:autocog_ista> -s ${SYNTAX} --search ${SEARCH} -p ${prompt_name} \$STA 2>/dev/null | \
                    $<TARGET_FILE:autocog_xfta> --rng --seed 42 /dev/stdin 2>/dev/null | \
                    $<TARGET_FILE:autocog_psta> -p ${prompt_name} \$STA > \$RESULT 2>/dev/null
                ${Python_EXECUTABLE} -c \"
import json, sys
with open('\$RESULT') as f: got = json.load(f)
with open('${golden_path}') as f: want = json.load(f)
if got != want:
    print('MISMATCH')
    print('got: ', json.dumps(got, indent=2)[:200])
    print('want:', json.dumps(want, indent=2)[:200])
    sys.exit(1)
\"
                rm -f \$STA \$FTA_OUT \$RESULT
            "
    )
    set_tests_properties("e2e_${test_label}" PROPERTIES
        LABELS "e2e"
        TIMEOUT 30
    )
endforeach()

# Pre-filled content round-trip test: all fields provided via -d, output must match exactly
set(DN_STL ${STL_FIXTURES}/language/structures/test_deep_nesting.stl)
set(DN_CONTENT ${CMAKE_CURRENT_SOURCE_DIR}/test_deep_nesting_content.json)
set(DN_GOLDEN ${CMAKE_CURRENT_SOURCE_DIR}/prefilled/test_deep_nesting.golden.json)

add_test(
    NAME e2e_deep_nesting_prefilled
    COMMAND ${CMAKE_COMMAND} -E env
        bash -c "
            set -e
            STA=$(mktemp) RESULT=$(mktemp)
            $<TARGET_FILE:autocog_stlc> --emit sta -I ${STLIB} ${DN_STL} > \$STA 2>/dev/null
            $<TARGET_FILE:autocog_ista> -s ${SYNTAX} --search ${SEARCH} -d ${DN_CONTENT} -p test \$STA 2>/dev/null | \
                $<TARGET_FILE:autocog_xfta> --rng --seed 42 /dev/stdin 2>/dev/null | \
                $<TARGET_FILE:autocog_psta> -p test \$STA > \$RESULT 2>/dev/null
            ${Python_EXECUTABLE} -c \"
import json, sys
with open('\$RESULT') as f: got = json.load(f)
with open('${DN_GOLDEN}') as f: want = json.load(f)
if got != want:
    print('MISMATCH')
    for k in set(list(got.keys()) + list(want.keys())):
        if got.get(k) != want.get(k):
            print(f'  {k}: got={json.dumps(got.get(k))[:80]}')
            print(f'  {k}: want={json.dumps(want.get(k))[:80]}')
    sys.exit(1)
\"
            rm -f \$STA \$RESULT
        "
)
set_tests_properties(e2e_deep_nesting_prefilled PROPERTIES
    LABELS "e2e;prefilled"
    TIMEOUT 30
)

# Variable-length array tests: same fixture with different content sizes
set(VA_STL ${STL_FIXTURES}/language/structures/test_variable_arrays.stl)
set(VA_DIR ${CMAKE_CURRENT_SOURCE_DIR}/variable)

foreach(variant min max mid mixed asym)
    set(VA_CONTENT ${VA_DIR}/${variant}.content.json)
    set(VA_GOLDEN ${VA_DIR}/${variant}.golden.json)

    add_test(
        NAME "e2e_variable_arrays_${variant}"
        COMMAND ${CMAKE_COMMAND} -E env
            bash -c "
                set -e
                STA=$(mktemp) RESULT=$(mktemp)
                $<TARGET_FILE:autocog_stlc> --emit sta -I ${STLIB} ${VA_STL} > \$STA 2>/dev/null
                $<TARGET_FILE:autocog_ista> -s ${SYNTAX} --search ${SEARCH} -d ${VA_CONTENT} -p test \$STA 2>/dev/null | \
                    $<TARGET_FILE:autocog_xfta> --rng --seed 42 /dev/stdin 2>/dev/null | \
                    $<TARGET_FILE:autocog_psta> -p test \$STA > \$RESULT 2>/dev/null
                ${Python_EXECUTABLE} -c \"
import json, sys
with open('\$RESULT') as f: got = json.load(f)
with open('${VA_GOLDEN}') as f: want = json.load(f)
if got != want:
    print('MISMATCH')
    for k in sorted(set(list(got.keys()) + list(want.keys()))):
        if got.get(k) != want.get(k):
            print(f'  {k}: got={json.dumps(got.get(k))[:80]}')
            print(f'  {k}: want={json.dumps(want.get(k))[:80]}')
    sys.exit(1)
\"
                rm -f \$STA \$RESULT
            "
    )
    set_tests_properties("e2e_variable_arrays_${variant}" PROPERTIES
        LABELS "e2e;prefilled;variable"
        TIMEOUT 30
    )
endforeach()

# Search variant test: exercise repetition penalty + diversity bonus code paths
# Uses full.json search config (beams=2, repetition=0.5, diversity=0.3)
set(SEARCH_FULL ${PROJECT_SOURCE_DIR}/share/search/full.json)
add_test(
    NAME e2e_search_full
    COMMAND ${CMAKE_COMMAND} -E env
        bash -c "
            set -e
            STA=$(mktemp) RESULT=$(mktemp)
            $<TARGET_FILE:autocog_stlc> --emit sta -I ${STLIB} ${STL_FIXTURES}/language/structures/test_prompt_with_fields.stl > \$STA 2>/dev/null
            $<TARGET_FILE:autocog_ista> -s ${SYNTAX} --search ${SEARCH_FULL} -p person \$STA 2>/dev/null | \
                $<TARGET_FILE:autocog_xfta> --rng --seed 42 /dev/stdin 2>/dev/null | \
                $<TARGET_FILE:autocog_psta> -p person \$STA > \$RESULT 2>/dev/null
            ${Python_EXECUTABLE} -c \"
import json, sys
with open('\$RESULT') as f: r = json.load(f)
assert isinstance(r, dict), f'expected dict, got {type(r)}'
assert 'name' in r, f'missing name field'
\"
            rm -f \$STA \$RESULT
        "
)
set_tests_properties(e2e_search_full PROPERTIES
    LABELS "e2e;search"
    TIMEOUT 60
)

# STL* round-trip: compile a fixture to IR, unparse to STL* via ir2stl, recompile
# to IR, and assert the vocab tables match. Guards that ir2stl emits the vocab
# table as declarations + exports such that the program recompiles to the same
# resolved vocab hashes.
set(VOCAB_STL ${STL_FIXTURES}/language/vocab/test_vocab.stl)
set(IR2STL ${PROJECT_SOURCE_DIR}/tools/ir2stl.py)
add_test(
    NAME e2e_vocab_stl_star_roundtrip
    COMMAND ${CMAKE_COMMAND} -E env
        bash -c "
            set -e
            IR1=$(mktemp) STAR=$(mktemp) IR2=$(mktemp)
            $<TARGET_FILE:autocog_stlc> --emit ir ${VOCAB_STL} 2>/dev/null | grep -v libgcov > \$IR1
            ${Python_EXECUTABLE} ${IR2STL} \$IR1 > \$STAR
            $<TARGET_FILE:autocog_stlc> --emit ir \$STAR 2>/dev/null | grep -v libgcov > \$IR2
            ${Python_EXECUTABLE} -c \"
import json, sys
a = json.load(open('\$IR1')); b = json.load(open('\$IR2'))
pa = next(iter(a['prompts'].values())); pb = next(iter(b['prompts'].values()))
va, vb = pa.get('vocabs', {}), pb.get('vocabs', {})
if va != vb:
    print('VOCAB TABLE MISMATCH'); print(' first:', sorted(va)); print(' round:', sorted(vb)); sys.exit(1)
ra = [f.get('format',{}).get('vocab') for f in pa['fields']]
rb = [f.get('format',{}).get('vocab') for f in pb['fields']]
if ra != rb:
    print('FIELD REF MISMATCH'); print(' first:', ra); print(' round:', rb); sys.exit(1)
print('OK: vocab table and refs stable across STL* round-trip (', len(va), 'entries )')
\"
            rm -f \$IR1 \$STAR \$IR2
        "
)
set_tests_properties(e2e_vocab_stl_star_roundtrip PROPERTIES
    LABELS "e2e;vocab"
    TIMEOUT 30
)
