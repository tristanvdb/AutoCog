
file(GLOB MCQ_STL_FILES ${CMAKE_CURRENT_SOURCE_DIR}/*.stl)

set(STLIB ${PROJECT_SOURCE_DIR}/share/library/stlib)
set(SYNTAX ${PROJECT_SOURCE_DIR}/share/syntax/default.json)
set(SEARCH ${PROJECT_SOURCE_DIR}/share/search/default.json)

foreach(STL_FILE ${MCQ_STL_FILES})
    get_filename_component(DEMO_NAME ${STL_FILE} NAME_WE)

    # Compile smoke
    add_test(
        NAME demo-stlc-mcq-${DEMO_NAME}
        COMMAND $<TARGET_FILE:autocog_stlc> --ir /dev/null -I ${STLIB} ${STL_FILE}
    )
    set_tests_properties(demo-stlc-mcq-${DEMO_NAME} PROPERTIES LABELS "stlc;demo")

    # Instantiation smoke: compile to STA, then instantiate the main entry prompt
    # with empty content.
    add_test(NAME demo-ista-mcq-${DEMO_NAME}
        COMMAND ${CMAKE_COMMAND} -E env
            bash -c "
                set -e
                STA=\$(mktemp)
                $<TARGET_FILE:autocog_stlc> --sta \$STA -I ${STLIB} ${STL_FILE} 2>/dev/null
                P=\$(${Python_EXECUTABLE} -c 'import json,sys; print(json.load(open(sys.argv[1]))[\"entry_points\"][\"main\"][\"prompt\"])' \$STA)
                $<TARGET_FILE:autocog_ista> --sta \$STA --prompt \$P --syntax ${SYNTAX} --search ${SEARCH} --content {} --fta /dev/null
                rm -f \$STA
            "
    )
    set_tests_properties(demo-ista-mcq-${DEMO_NAME} PROPERTIES LABELS "ista;demo")
endforeach()
