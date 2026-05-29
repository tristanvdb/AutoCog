# psta integration tests

set(STA_GOLDEN ${PROJECT_SOURCE_DIR}/tests/integration/tools/stlc/sta)

add_test(NAME psta_smoke
         COMMAND $<TARGET_FILE:autocog_psta> --help)
set_tests_properties(psta_smoke PROPERTIES LABELS "smoke;psta")

# Minimal FTT JSON → psta parses to empty result
add_test(NAME psta_args_prompt
         COMMAND ${CMAKE_COMMAND} -E env
             bash -c "echo '{\"action\":0,\"uid\":\"header\",\"text\":\"\",\"logprob\":0,\"logprobs\":[],\"length\":0,\"pruned\":false,\"tokens\":[],\"children\":[]}' | $<TARGET_FILE:autocog_psta> -p test ${STA_GOLDEN}/language/structures/test_basic_record.golden.json > /dev/null")
set_tests_properties(psta_args_prompt PROPERTIES LABELS "psta;args")

# -o output file
add_test(NAME psta_args_output
         COMMAND ${CMAKE_COMMAND} -E env
             bash -c "TMPF=\$(mktemp) && echo '{\"action\":0,\"uid\":\"header\",\"text\":\"\",\"logprob\":0,\"logprobs\":[],\"length\":0,\"pruned\":false,\"tokens\":[],\"children\":[]}' | $<TARGET_FILE:autocog_psta> -o \$TMPF ${STA_GOLDEN}/language/structures/test_basic_record.golden.json && test -s \$TMPF && rm -f \$TMPF")
set_tests_properties(psta_args_output PROPERTIES LABELS "psta;args")

# -i input file
add_test(NAME psta_args_input_file
         COMMAND ${CMAKE_COMMAND} -E env
             bash -c "TMPF=\$(mktemp) && echo '{\"action\":0,\"uid\":\"header\",\"text\":\"\",\"logprob\":0,\"logprobs\":[],\"length\":0,\"pruned\":false,\"tokens\":[],\"children\":[]}' > \$TMPF && $<TARGET_FILE:autocog_psta> -i \$TMPF ${STA_GOLDEN}/language/structures/test_basic_record.golden.json > /dev/null && rm -f \$TMPF")
set_tests_properties(psta_args_input_file PROPERTIES LABELS "psta;args")

# Full pipeline: ista → xfta → psta
add_test(NAME psta_pipeline
         COMMAND ${CMAKE_COMMAND} -E env
             bash -c "
                 set -e
                 FTA=\$(mktemp)
                 $<TARGET_FILE:autocog_ista> -s ${PROJECT_SOURCE_DIR}/share/syntax/default.json --search ${PROJECT_SOURCE_DIR}/share/search/default.json -o \$FTA ${STA_GOLDEN}/language/structures/test_basic_record.golden.json 2>/dev/null
                 $<TARGET_FILE:autocog_xfta> --rng \$FTA | $<TARGET_FILE:autocog_psta> ${STA_GOLDEN}/language/structures/test_basic_record.golden.json > /dev/null
                 rm -f \$FTA
             ")
set_tests_properties(psta_pipeline PROPERTIES LABELS "psta;e2e" TIMEOUT 30)
