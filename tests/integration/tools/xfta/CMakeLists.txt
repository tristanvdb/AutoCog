# xfta integration tests

set(FTA_FIXTURES ${PROJECT_SOURCE_DIR}/tests/fixtures/fta)

# Tokenized FTA fixture tests — output FTT JSON to stdout
add_test(NAME xfta_text_completion
         COMMAND ${CMAKE_COMMAND} -E env
             bash -c "$<TARGET_FILE:autocog_xfta> --rng --seed 42 ${FTA_FIXTURES}/test_text_completion.json > /dev/null")
set_tests_properties(xfta_text_completion PROPERTIES LABELS "xfta")

add_test(NAME xfta_text_choice
         COMMAND ${CMAKE_COMMAND} -E env
             bash -c "$<TARGET_FILE:autocog_xfta> --rng --seed 42 ${FTA_FIXTURES}/test_text_choice.json > /dev/null")
set_tests_properties(xfta_text_choice PROPERTIES LABELS "xfta")

# CLI argument tests
add_test(NAME xfta_args_verbose
         COMMAND ${CMAKE_COMMAND} -E env
             bash -c "$<TARGET_FILE:autocog_xfta> --rng --seed 42 -v ${FTA_FIXTURES}/test_text_choice.json > /dev/null")
set_tests_properties(xfta_args_verbose PROPERTIES LABELS "xfta;args")

# Output to file
add_test(NAME xfta_args_output_file
         COMMAND ${CMAKE_COMMAND} -E env
             bash -c "TMPF=$(mktemp) && $<TARGET_FILE:autocog_xfta> --rng --seed 42 -o \$TMPF ${FTA_FIXTURES}/test_text_choice.json && test -s \$TMPF && rm -f \$TMPF")
set_tests_properties(xfta_args_output_file PROPERTIES LABELS "xfta;args")

# Multiple input files
add_test(NAME xfta_args_multi_input
         COMMAND ${CMAKE_COMMAND} -E env
             bash -c "$<TARGET_FILE:autocog_xfta> --rng --seed 42 ${FTA_FIXTURES}/test_text_completion.json ${FTA_FIXTURES}/test_text_choice.json > /dev/null")
set_tests_properties(xfta_args_multi_input PROPERTIES LABELS "xfta;args")
