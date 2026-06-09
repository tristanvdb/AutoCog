# stlc error-path tests: invalid STL must be rejected with a proper diagnostic.
#
# Each fixture declares the diagnostic it expects with an in-fixture directive:
#     // expect-error: <substring>
# check_error_output.py matches that substring against stlc output, requires a
# non-zero exit, and fails on any "internal error" (never an acceptable
# diagnostic). Un-annotated fixtures fall back to a generic error-message check.

set(FIXTURES ${PROJECT_SOURCE_DIR}/tests/fixtures/stl/errors)
set(CHECK ${PROJECT_SOURCE_DIR}/tests/scripts/check_error_output.py)

function(add_error_test fixture_path)
    file(RELATIVE_PATH rel ${FIXTURES} ${fixture_path})
    string(REPLACE ".stl" "" test_name ${rel})

    add_test(
        NAME stlc_error_${test_name}
        COMMAND ${CMAKE_COMMAND} -E env bash -c "
            OUTPUT=$($<TARGET_FILE:autocog_stlc> --ir /dev/stdout ${fixture_path} 2>&1) ;
            RC=\$? ;
            echo \"\$OUTPUT\" | python3 ${CHECK} ${fixture_path} \$RC
        "
    )
    set_tests_properties(stlc_error_${test_name} PROPERTIES LABELS "stlc;errors")
endfunction()

file(GLOB ERROR_FIXTURES ${FIXTURES}/*.stl)
foreach(stl ${ERROR_FIXTURES})
    add_error_test(${stl})
endforeach()
