# stlc error path tests — verify invalid STL produces errors

set(FIXTURES ${PROJECT_SOURCE_DIR}/tests/fixtures/stl/errors)

function(add_error_test fixture_path)
    file(RELATIVE_PATH rel ${FIXTURES} ${fixture_path})
    string(REPLACE ".stl" "" test_name ${rel})

    # Test that stlc returns non-zero AND outputs an error message
    add_test(
        NAME stlc_error_${test_name}
        COMMAND ${CMAKE_COMMAND} -E env bash -c "
            OUTPUT=$($<TARGET_FILE:autocog_stlc> --emit ir ${fixture_path} 2>&1) ;
            RC=\$? ;
            if [ \$RC -eq 0 ]; then
                echo 'FAIL: expected non-zero exit code' ;
                exit 1 ;
            fi ;
            if ! echo \"\$OUTPUT\" | grep -qi 'error\\|failed'; then
                echo 'FAIL: expected error message in output' ;
                echo \"\$OUTPUT\" ;
                exit 1 ;
            fi ;
            echo 'OK: stlc rejected with rc='\$RC
        "
    )
    set_tests_properties(stlc_error_${test_name} PROPERTIES LABELS "stlc;errors")
endfunction()

file(GLOB ERROR_FIXTURES ${FIXTURES}/*.stl)
foreach(stl ${ERROR_FIXTURES})
    add_error_test(${stl})
endforeach()
