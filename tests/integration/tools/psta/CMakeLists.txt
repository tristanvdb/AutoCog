# psta integration tests

set(STA_GOLDEN ${PROJECT_SOURCE_DIR}/tests/integration/tools/stlc/sta)
set(SYNTAX ${PROJECT_SOURCE_DIR}/share/syntax/default.json)
set(SEARCH ${PROJECT_SOURCE_DIR}/share/search/default.json)
set(BR ${STA_GOLDEN}/language/structures/test_basic_record.golden.json)

# A minimal (metadata-less) FTT: a single empty root node.
set(MIN_FTT ${CMAKE_CURRENT_BINARY_DIR}/min_ftt.json)
file(WRITE ${MIN_FTT}
  "{\"action\":0,\"uid\":\"header\",\"text\":\"\",\"logprob\":0,\"logprobs\":[],\"length\":0,\"pruned\":false,\"tokens\":[],\"children\":[],\"provenance\":{}}")

add_test(NAME psta_smoke COMMAND $<TARGET_FILE:autocog_psta> --help)
set_tests_properties(psta_smoke PROPERTIES LABELS "smoke;psta")

# Parse a minimal FTT into a frame
add_test(NAME psta_args_prompt
         COMMAND $<TARGET_FILE:autocog_psta> --sta ${BR} --ftt ${MIN_FTT} --prompt test --frame /dev/null)
set_tests_properties(psta_args_prompt PROPERTIES LABELS "psta;args")

# --frame writes the parsed frame to a file
add_test(NAME psta_args_frame_file
         COMMAND ${CMAKE_COMMAND} -E env bash -c "
             TMPF=\$(mktemp) &&
             $<TARGET_FILE:autocog_psta> --sta ${BR} --ftt ${MIN_FTT} --prompt test --frame \$TMPF &&
             test -s \$TMPF && rm -f \$TMPF")
set_tests_properties(psta_args_frame_file PROPERTIES LABELS "psta;args")

# Full pipeline: ista -> xfta -> psta
add_test(NAME psta_pipeline
         COMMAND ${CMAKE_COMMAND} -E env
             bash -c "
                 set -e
                 FTA=\$(mktemp) FTT=\$(mktemp)
                 $<TARGET_FILE:autocog_ista> --sta ${BR} --prompt test --syntax ${SYNTAX} --search ${SEARCH} --content {} --fta \$FTA 2>/dev/null
                 $<TARGET_FILE:autocog_xfta> --rng --seed 42 --fta \$FTA --ftt \$FTT 2>/dev/null
                 $<TARGET_FILE:autocog_psta> --sta ${BR} --ftt \$FTT --prompt test --frame /dev/null
                 rm -f \$FTA \$FTT
             ")
set_tests_properties(psta_pipeline PROPERTIES LABELS "psta;e2e" TIMEOUT 30)

# Error paths (must exit non-zero): each required option omitted
add_test(NAME psta_err_missing_sta
         COMMAND $<TARGET_FILE:autocog_psta> --ftt ${MIN_FTT} --prompt test --frame /dev/null)
set_tests_properties(psta_err_missing_sta PROPERTIES WILL_FAIL TRUE LABELS "psta;args")

add_test(NAME psta_err_missing_ftt
         COMMAND $<TARGET_FILE:autocog_psta> --sta ${BR} --prompt test --frame /dev/null)
set_tests_properties(psta_err_missing_ftt PROPERTIES WILL_FAIL TRUE LABELS "psta;args")

add_test(NAME psta_err_missing_prompt
         COMMAND $<TARGET_FILE:autocog_psta> --sta ${BR} --ftt ${MIN_FTT} --frame /dev/null)
set_tests_properties(psta_err_missing_prompt PROPERTIES WILL_FAIL TRUE LABELS "psta;args")

add_test(NAME psta_err_bad_prompt
         COMMAND $<TARGET_FILE:autocog_psta> --sta ${BR} --ftt ${MIN_FTT} --prompt nope --frame /dev/null)
set_tests_properties(psta_err_bad_prompt PROPERTIES WILL_FAIL TRUE LABELS "psta;args")
