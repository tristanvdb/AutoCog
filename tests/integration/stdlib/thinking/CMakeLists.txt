# Integration tests for the thinking stdlib (share/library/stlib/thinking/).
#
# Each fixture in fixtures/ is a driver program that imports one library
# component and exercises it (record use, prompt call + bind). Two tests per
# driver:
#   stdlib-stlc-thinking-<name>  compile smoke (stlc --ir)
#   stdlib-e2e-thinking-<name>   full pipeline: stlc -> ista -> xfta --rng -> psta,
#                                asserting the frame parses as JSON.

file(GLOB THINKING_DRIVERS ${CMAKE_CURRENT_SOURCE_DIR}/fixtures/*.stl)

set(STLIB ${PROJECT_SOURCE_DIR}/share/library/stlib)
set(SYNTAX ${PROJECT_SOURCE_DIR}/share/syntax/complete.json)
set(SEARCH ${PROJECT_SOURCE_DIR}/share/search/default.json)

foreach(STL_FILE ${THINKING_DRIVERS})
    get_filename_component(DRIVER_NAME ${STL_FILE} NAME_WE)

    add_test(
        NAME stdlib-stlc-thinking-${DRIVER_NAME}
        COMMAND $<TARGET_FILE:autocog_stlc> --ir /dev/null -I ${STLIB} ${STL_FILE}
    )
    set_tests_properties(stdlib-stlc-thinking-${DRIVER_NAME} PROPERTIES LABELS "stlc;stdlib")

    add_test(NAME stdlib-e2e-thinking-${DRIVER_NAME}
        COMMAND ${CMAKE_COMMAND} -E env
            bash -c "
                set -e
                STA=\$(mktemp) FTA=\$(mktemp) FTT=\$(mktemp) RESULT=\$(mktemp)
                trap 'rm -f \$STA \$FTA \$FTT \$RESULT' EXIT
                $<TARGET_FILE:autocog_stlc> --sta \$STA -I ${STLIB} ${STL_FILE} 2>/dev/null
                P=\$(${Python_EXECUTABLE} -c 'import json,sys; print(json.load(open(sys.argv[1]))[\"entry_points\"][\"main\"][\"prompt\"])' \$STA)
                $<TARGET_FILE:autocog_ista> --sta \$STA --prompt \$P --syntax ${SYNTAX} --search ${SEARCH} --content {} --fta \$FTA 2>/dev/null
                $<TARGET_FILE:autocog_xfta> --rng --seed 42 --fta \$FTA --ftt \$FTT 2>/dev/null
                $<TARGET_FILE:autocog_psta> --sta \$STA --ftt \$FTT --prompt \$P --frame \$RESULT 2>/dev/null
                ${Python_EXECUTABLE} -c 'import json,sys; json.load(open(sys.argv[1]))' \$RESULT
            "
    )
    set_tests_properties(stdlib-e2e-thinking-${DRIVER_NAME} PROPERTIES LABELS "e2e;stdlib")
endforeach()
