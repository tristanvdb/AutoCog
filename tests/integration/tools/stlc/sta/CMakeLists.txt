# stlc --emit sta golden output tests

set(FIXTURES ${PROJECT_SOURCE_DIR}/tests/fixtures/stl)
set(GOLDEN_SRC ${CMAKE_CURRENT_SOURCE_DIR})
set(GOLDEN ${CMAKE_CURRENT_BINARY_DIR}/golden)

# Copy golden files to build tree, replacing ABI_VERSION with actual version
file(READ "${PROJECT_SOURCE_DIR}/VERSION" _AUTOCOG_VERSION)
string(STRIP "${_AUTOCOG_VERSION}" _AUTOCOG_VERSION)
string(REGEX REPLACE "^v" "" _AUTOCOG_VERSION "${_AUTOCOG_VERSION}")

file(GLOB_RECURSE GOLDEN_FILES ${GOLDEN_SRC}/*.golden.json)
foreach(gf ${GOLDEN_FILES})
    file(RELATIVE_PATH rel ${GOLDEN_SRC} ${gf})
    set(dst ${GOLDEN}/${rel})
    get_filename_component(dst_dir ${dst} DIRECTORY)
    file(MAKE_DIRECTORY ${dst_dir})
    file(READ ${gf} content)
    string(REPLACE "ABI_VERSION" "${_AUTOCOG_VERSION}" content "${content}")
    file(WRITE ${dst} "${content}")
endforeach()

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
            diff -u ${GOLDEN}/${rel_noext}.golden.json \$TMPF &&
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
