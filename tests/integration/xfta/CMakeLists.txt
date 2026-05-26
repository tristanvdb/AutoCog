# xFTA Integration Tests
#
# These tests exercise the FTA evaluation pipeline using the built-in RNG model.
# Each test runs xfta --rng on a handwritten FTA JSON fixture.

function(add_xfta_rng_test test_name json_file)
  add_test(
    NAME xfta_${test_name}
    COMMAND $<TARGET_FILE:autocog_xfta> --rng ${CMAKE_CURRENT_SOURCE_DIR}/${json_file}
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
  )
  set_tests_properties(xfta_${test_name} PROPERTIES
    LABELS "xfta"
  )
endfunction()

add_xfta_rng_test(text_completion test_text_completion.json)
add_xfta_rng_test(text_choice     test_text_choice.json)

# ============================================================================
# End-to-end tests: stlc → ista → xfta --rng
# ============================================================================

function(add_e2e_test test_name stl_file data_args)
    add_test(
        NAME e2e_${test_name}
        COMMAND ${CMAKE_COMMAND} -E env
            bash -c "
                STA=\$(mktemp) && FTA=\$(mktemp) &&
                $<TARGET_FILE:autocog_stlc> --emit sta -I ${PROJECT_SOURCE_DIR}/share/library ${stl_file} -o \$STA 2>/dev/null &&
                $<TARGET_FILE:autocog_ista> ${data_args} -o \$FTA \$STA 2>/dev/null &&
                $<TARGET_FILE:autocog_xfta> --rng \$FTA 2>/dev/null &&
                test -f \$FTA.ftt.json &&
                rm -f \$STA \$FTA \$FTA.ftt.json
            "
    )
    set_tests_properties(e2e_${test_name} PROPERTIES LABELS "e2e;stlc;ista;xfta" TIMEOUT 30)
endfunction()

# Pre-filled content file for MCQ tests (avoids expensive completions)
file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/mcq_data.json
     "{\"topic\":\"Sci\",\"question\":\"2+2?\",\"choices\":[\"3\",\"4\",\"5\",\"6\"]}")

set(MCQ_DATA "-d ${CMAKE_CURRENT_BINARY_DIR}/mcq_data.json")

add_e2e_test(select      ${PROJECT_SOURCE_DIR}/share/demos/mcq/select.stl     "${MCQ_DATA}")
add_e2e_test(repeat      ${PROJECT_SOURCE_DIR}/share/demos/mcq/repeat.stl     "${MCQ_DATA}")

# ============================================================================
# Full pipeline test: stlc → ista → xfta --rng --best → psta
# Verifies the parsed result has expected fields
# ============================================================================

file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/e2e_content.json
     "{\"topic\":\"Sci\",\"question\":\"2+2?\",\"choices\":[\"3\",\"4\",\"5\",\"6\"]}")

add_test(
    NAME e2e_full_pipeline
    COMMAND ${CMAKE_COMMAND} -E env
        bash -c "
            set -e
            STA=\$(mktemp) FTA=\$(mktemp) RESULT=\$(mktemp)
            $<TARGET_FILE:autocog_stlc> --emit sta ${PROJECT_SOURCE_DIR}/share/demos/mcq/select.stl -o \$STA 2>/dev/null
            $<TARGET_FILE:autocog_ista> -d ${CMAKE_CURRENT_BINARY_DIR}/e2e_content.json -o \$FTA \$STA 2>/dev/null
            $<TARGET_FILE:autocog_xfta> --rng --best \$FTA 2>/dev/null | $<TARGET_FILE:autocog_psta> \$STA > \$RESULT
            ${Python_EXECUTABLE} -c \"
import json, sys
r = json.load(open('\$RESULT'))
assert 'topic' in r, 'missing topic'
assert 'question' in r, 'missing question'
assert 'choices' in r, 'missing choices'
assert 'answer' in r, 'missing answer'
assert 'next' in r, 'missing next'
assert r['topic'] == 'Sci', f'wrong topic: {r[\"topic\"]}'
assert r['question'] == '2+2?', f'wrong question: {r[\"question\"]}'
assert len(r['choices']) == 4, f'wrong choices count: {len(r[\"choices\"])}'
assert r['next'] == 'return', f'wrong next: {r[\"next\"]}'
print('OK: all fields match')
\"
            rm -f \$STA \$FTA \$RESULT
        "
)
set_tests_properties(e2e_full_pipeline PROPERTIES
    LABELS "e2e;stlc;ista;xfta;psta"
    TIMEOUT 30
)
