# ista integration tests — instantiate STA goldens into FTAs (smoke).
# Each test feeds a checked-in STA golden through ista with empty content.

set(STA_GOLDEN ${PROJECT_SOURCE_DIR}/tests/integration/tools/stlc/sta)
set(SYNTAX ${PROJECT_SOURCE_DIR}/share/syntax/default.json)
set(SEARCH ${PROJECT_SOURCE_DIR}/share/search/default.json)

function(add_ista_test test_name sta_golden prompt)
    add_test(
        NAME ista_${test_name}
        COMMAND $<TARGET_FILE:autocog_ista>
            --sta ${sta_golden} --prompt ${prompt}
            --syntax ${SYNTAX} --search ${SEARCH} --content {}
            --fta /dev/null
    )
    set_tests_properties(ista_${test_name} PROPERTIES LABELS "ista")
endfunction()

add_ista_test(basic_record   ${STA_GOLDEN}/language/structures/test_basic_record.golden.json        test)
add_ista_test(array_fields   ${STA_GOLDEN}/language/structures/test_array_fields.golden.json         collector)
add_ista_test(choice_formats ${STA_GOLDEN}/language/formats/test_choice_formats.golden.json          chooser)
add_ista_test(annotations    ${STA_GOLDEN}/language/annotations/test_annotations.golden.json         questioner)
add_ista_test(flow_loop      ${STA_GOLDEN}/flows/control/test_flow_loop.golden.json                  solver)
add_ista_test(return_labeled ${STA_GOLDEN}/flows/return/test_return_labeled.golden.json              processor)
add_ista_test(import         ${STA_GOLDEN}/language/symbols/test_import.golden.json                  main)
add_ista_test(parameterized  ${STA_GOLDEN}/language/expressions/test_parameterized_prompt.golden.json worker__10_6390854715352638587)
add_ista_test(call_basic     ${STA_GOLDEN}/flows/calls/test_call_basic.golden.json                  main)
add_ista_test(vocab          ${STA_GOLDEN}/language/vocab/test_vocab.golden.json                    writer)

# ============================================================================
# CLI argument tests (long-only interface)
# ============================================================================
set(BR ${STA_GOLDEN}/language/structures/test_basic_record.golden.json)

# --fta writes the FTA to a file
add_test(NAME ista_args_fta_file
         COMMAND ${CMAKE_COMMAND} -E env bash -c "
             TMPF=\$(mktemp) &&
             $<TARGET_FILE:autocog_ista> --sta ${BR} --prompt test --syntax ${SYNTAX} --search ${SEARCH} --content {} --fta \$TMPF &&
             test -s \$TMPF && rm -f \$TMPF
         ")
set_tests_properties(ista_args_fta_file PROPERTIES LABELS "ista;args")

# --content accepts inline JSON (file-or-JSON convenience)
add_test(NAME ista_args_content_inline
         COMMAND $<TARGET_FILE:autocog_ista>
             --sta ${BR} --prompt test --syntax ${SYNTAX} --search ${SEARCH}
             --content {} --fta /dev/null)
set_tests_properties(ista_args_content_inline PROPERTIES LABELS "ista;args")

# Error paths (must exit non-zero): each required option omitted, plus a bad prompt
add_test(NAME ista_err_missing_sta
         COMMAND $<TARGET_FILE:autocog_ista> --prompt test --syntax ${SYNTAX} --search ${SEARCH} --content {} --fta /dev/null)
set_tests_properties(ista_err_missing_sta PROPERTIES WILL_FAIL TRUE LABELS "ista;args")

add_test(NAME ista_err_missing_fta
         COMMAND $<TARGET_FILE:autocog_ista> --sta ${BR} --prompt test --syntax ${SYNTAX} --search ${SEARCH} --content {})
set_tests_properties(ista_err_missing_fta PROPERTIES WILL_FAIL TRUE LABELS "ista;args")

add_test(NAME ista_err_unknown_opt
         COMMAND $<TARGET_FILE:autocog_ista> --bogus --sta ${BR} --prompt test --syntax ${SYNTAX} --search ${SEARCH} --content {} --fta /dev/null)
set_tests_properties(ista_err_unknown_opt PROPERTIES WILL_FAIL TRUE LABELS "ista;args")

add_test(NAME ista_err_bad_prompt
         COMMAND $<TARGET_FILE:autocog_ista> --sta ${BR} --prompt nope --syntax ${SYNTAX} --search ${SEARCH} --content {} --fta /dev/null)
set_tests_properties(ista_err_bad_prompt PROPERTIES WILL_FAIL TRUE LABELS "ista;args")
