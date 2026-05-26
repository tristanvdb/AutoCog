# xfta integration tests

set(FTA_FIXTURES ${PROJECT_SOURCE_DIR}/tests/fixtures/fta)

# Tokenized FTA fixture tests
add_test(NAME xfta_text_completion
         COMMAND $<TARGET_FILE:autocog_xfta> --rng ${FTA_FIXTURES}/test_text_completion.json
         WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
set_tests_properties(xfta_text_completion PROPERTIES LABELS "xfta")

add_test(NAME xfta_text_choice
         COMMAND $<TARGET_FILE:autocog_xfta> --rng ${FTA_FIXTURES}/test_text_choice.json
         WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
set_tests_properties(xfta_text_choice PROPERTIES LABELS "xfta")

# ============================================================================
# CLI argument tests
# ============================================================================

# --verbose
add_test(NAME xfta_args_verbose
         COMMAND $<TARGET_FILE:autocog_xfta> --rng -v ${FTA_FIXTURES}/test_text_choice.json
         WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
set_tests_properties(xfta_args_verbose PROPERTIES LABELS "xfta;args")

# --best (text output instead of FTT file)
add_test(NAME xfta_args_best
         COMMAND ${CMAKE_COMMAND} -E env
             bash -c "$<TARGET_FILE:autocog_xfta> --rng --best ${FTA_FIXTURES}/test_text_choice.json 2>/dev/null | head -1 | grep -q '.'")
set_tests_properties(xfta_args_best PROPERTIES LABELS "xfta;args")

# --best --verbose combined
add_test(NAME xfta_args_best_verbose
         COMMAND $<TARGET_FILE:autocog_xfta> --rng --best -v ${FTA_FIXTURES}/test_text_completion.json
         WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
set_tests_properties(xfta_args_best_verbose PROPERTIES LABELS "xfta;args")

# Multiple input files
add_test(NAME xfta_args_multi_input
         COMMAND $<TARGET_FILE:autocog_xfta> --rng
             ${FTA_FIXTURES}/test_text_completion.json
             ${FTA_FIXTURES}/test_text_choice.json
         WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
set_tests_properties(xfta_args_multi_input PROPERTIES LABELS "xfta;args")
