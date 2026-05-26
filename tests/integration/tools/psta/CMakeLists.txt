# psta integration tests

set(STA_GOLDEN ${PROJECT_SOURCE_DIR}/tests/integration/tools/stlc/sta)

add_test(NAME psta_smoke
         COMMAND $<TARGET_FILE:autocog_psta> --help)
set_tests_properties(psta_smoke PROPERTIES LABELS "smoke;psta")

# -p prompt name
add_test(NAME psta_args_prompt
         COMMAND ${CMAKE_COMMAND} -E env
             bash -c "echo 'start:' | $<TARGET_FILE:autocog_psta> -p test ${STA_GOLDEN}/language/structures/test_basic_record.golden.json > /dev/null")
set_tests_properties(psta_args_prompt PROPERTIES LABELS "psta;args")

# -s syntax file
add_test(NAME psta_args_syntax
         COMMAND ${CMAKE_COMMAND} -E env
             bash -c "echo 'start:' | $<TARGET_FILE:autocog_psta> -s ${PROJECT_SOURCE_DIR}/share/syntax/default.json ${STA_GOLDEN}/language/structures/test_basic_record.golden.json > /dev/null")
set_tests_properties(psta_args_syntax PROPERTIES LABELS "psta;args")

# -o output file
add_test(NAME psta_args_output
         COMMAND ${CMAKE_COMMAND} -E env
             bash -c "TMPF=\$(mktemp) && echo 'start:' | $<TARGET_FILE:autocog_psta> -o \$TMPF ${STA_GOLDEN}/language/structures/test_basic_record.golden.json && test -s \$TMPF && rm -f \$TMPF")
set_tests_properties(psta_args_output PROPERTIES LABELS "psta;args")

# -i input file
add_test(NAME psta_args_input_file
         COMMAND ${CMAKE_COMMAND} -E env
             bash -c "TMPF=\$(mktemp) && echo 'start:' > \$TMPF && $<TARGET_FILE:autocog_psta> -i \$TMPF ${STA_GOLDEN}/language/structures/test_basic_record.golden.json > /dev/null && rm -f \$TMPF")
set_tests_properties(psta_args_input_file PROPERTIES LABELS "psta;args")
