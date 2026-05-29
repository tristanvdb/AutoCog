# ista integration tests — instantiate STA fixtures

set(FIXTURES ${PROJECT_SOURCE_DIR}/tests/fixtures/stl)
set(STA_GOLDEN ${PROJECT_SOURCE_DIR}/tests/integration/tools/stlc/sta)

function(add_ista_test test_name sta_golden)
    add_test(
        NAME ista_${test_name}
        COMMAND ${CMAKE_COMMAND} -E env
            bash -c "$<TARGET_FILE:autocog_ista> -s ${PROJECT_SOURCE_DIR}/share/syntax/default.json --search ${PROJECT_SOURCE_DIR}/share/search/default.json --text ${sta_golden} > /dev/null"
    )
    set_tests_properties(ista_${test_name} PROPERTIES LABELS "ista")
endfunction()

# Test instantiation on representative fixtures (using STA golden outputs)
add_ista_test(basic_record        ${STA_GOLDEN}/language/structures/test_basic_record.golden.json)
add_ista_test(array_fields        ${STA_GOLDEN}/language/structures/test_array_fields.golden.json)
add_ista_test(choice_formats      ${STA_GOLDEN}/language/formats/test_choice_formats.golden.json)
add_ista_test(annotations         ${STA_GOLDEN}/language/annotations/test_annotations.golden.json)
add_ista_test(flow_loop           ${STA_GOLDEN}/flows/control/test_flow_loop.golden.json)
add_ista_test(return_labeled      ${STA_GOLDEN}/flows/return/test_return_labeled.golden.json)
add_ista_test(import              ${STA_GOLDEN}/language/symbols/test_import.golden.json)
add_ista_test(parameterized       ${STA_GOLDEN}/language/expressions/test_parameterized_prompt.golden.json)
add_ista_test(call_basic          ${STA_GOLDEN}/flows/calls/test_call_basic.golden.json)

# ============================================================================
# CLI argument tests
# ============================================================================

# -p prompt name
add_test(NAME ista_args_prompt
         COMMAND ${CMAKE_COMMAND} -E env
             bash -c "$<TARGET_FILE:autocog_ista> -s ${PROJECT_SOURCE_DIR}/share/syntax/default.json --search ${PROJECT_SOURCE_DIR}/share/search/default.json -p test --text ${STA_GOLDEN}/language/structures/test_basic_record.golden.json > /dev/null")
set_tests_properties(ista_args_prompt PROPERTIES LABELS "ista;args")

# -s syntax file
add_test(NAME ista_args_syntax
         COMMAND ${CMAKE_COMMAND} -E env
             bash -c "$<TARGET_FILE:autocog_ista> -s ${PROJECT_SOURCE_DIR}/share/syntax/default.json --search ${PROJECT_SOURCE_DIR}/share/search/default.json --text ${STA_GOLDEN}/language/structures/test_basic_record.golden.json > /dev/null")
set_tests_properties(ista_args_syntax PROPERTIES LABELS "ista;args")

# -o output file
add_test(NAME ista_args_output
         COMMAND ${CMAKE_COMMAND} -E env
             bash -c "TMPF=\$(mktemp) && $<TARGET_FILE:autocog_ista> -s ${PROJECT_SOURCE_DIR}/share/syntax/default.json --search ${PROJECT_SOURCE_DIR}/share/search/default.json -o \$TMPF ${STA_GOLDEN}/language/structures/test_basic_record.golden.json && test -s \$TMPF && rm -f \$TMPF")
set_tests_properties(ista_args_output PROPERTIES LABELS "ista;args")

# -d data file
file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/test_data.json "{\"topic\":\"test\"}")
add_test(NAME ista_args_data
         COMMAND ${CMAKE_COMMAND} -E env
             bash -c "$<TARGET_FILE:autocog_ista> -s ${PROJECT_SOURCE_DIR}/share/syntax/default.json --search ${PROJECT_SOURCE_DIR}/share/search/default.json -d ${CMAKE_CURRENT_BINARY_DIR}/test_data.json --text ${STA_GOLDEN}/language/structures/test_basic_record.golden.json > /dev/null")
set_tests_properties(ista_args_data PROPERTIES LABELS "ista;args")
