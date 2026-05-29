# stlc --emit sta golden output tests

set(FIXTURES ${PROJECT_SOURCE_DIR}/tests/fixtures/stl)
set(GOLDEN ${CMAKE_CURRENT_SOURCE_DIR})
set(COMPARE ${PROJECT_SOURCE_DIR}/tests/scripts/golden_compare.py)

function(add_sta_test fixture_path)
    # Derive test name from fixture path relative to FIXTURES
    file(RELATIVE_PATH rel ${FIXTURES} ${fixture_path})
    string(REPLACE "/" "_" flat_name ${rel})
    string(REPLACE ".stl" "" flat_name ${flat_name})
    string(REPLACE ".stl" "" rel_noext ${rel})

    set(extra_args ${ARGN})
    add_test(
        NAME stlc_sta_${flat_name}
        COMMAND ${CMAKE_COMMAND} -E env bash -c "
            TMPF=$(mktemp) &&
            $<TARGET_FILE:autocog_stlc> --emit sta ${extra_args} ${fixture_path} > \$TMPF 2>/dev/null &&
            python3 ${COMPARE} --force-validate \$TMPF ${GOLDEN}/${rel_noext}.golden.json &&
            rm -f \$TMPF
        "
    )
    set_tests_properties(stlc_sta_${flat_name} PROPERTIES LABELS "stlc;sta;golden")
endfunction()

# Glob all fixtures and generate tests
file(GLOB_RECURSE ALL_STL ${FIXTURES}/*.stl)
foreach(stl ${ALL_STL})
    get_filename_component(name ${stl} NAME)
    if(NOT name MATCHES "_lib")
        # Skip error fixtures (tested separately)
        if(stl MATCHES "/errors/")
            continue()
        endif()
        # Add -I for symbol tests that need import resolution
        if(stl MATCHES "language/symbols")
            add_sta_test(${stl} "-I ${FIXTURES}/language/symbols")
        else()
            add_sta_test(${stl})
        endif()
    endif()
endforeach()
