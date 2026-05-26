# End-to-end pipeline tests: stlc → ista → xfta --rng --best → psta

set(STA_GOLDEN ${CMAKE_BINARY_DIR}/tests/integration/tools/stlc/sta/golden)

file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/mcq_data.json
     "{\"topic\":\"Sci\",\"question\":\"2+2?\",\"choices\":[\"3\",\"4\",\"5\",\"6\"]}")

add_test(
    NAME e2e_select
    COMMAND ${CMAKE_COMMAND} -E env
        bash -c "
            set -e
            FTA=\$(mktemp) RESULT=\$(mktemp)
            $<TARGET_FILE:autocog_ista> -d ${CMAKE_CURRENT_BINARY_DIR}/mcq_data.json -o \$FTA ${STA_GOLDEN}/language/structures/test_basic_record.golden.json 2>/dev/null
            $<TARGET_FILE:autocog_xfta> --rng --best \$FTA 2>/dev/null | $<TARGET_FILE:autocog_psta> ${STA_GOLDEN}/language/structures/test_basic_record.golden.json > \$RESULT
            ${Python_EXECUTABLE} -c \"
import json
r = json.load(open('\$RESULT'))
assert isinstance(r, dict), f'expected dict, got {type(r)}'
print('OK')
\"
            rm -f \$FTA \$RESULT
        "
)
set_tests_properties(e2e_select PROPERTIES LABELS "e2e" TIMEOUT 30)
