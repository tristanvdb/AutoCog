
file(GLOB MCQ_STL_FILES ${CMAKE_CURRENT_SOURCE_DIR}/*.stl)

foreach(STL_FILE ${MCQ_STL_FILES})
    get_filename_component(DEMO_NAME ${STL_FILE} NAME_WE)
    add_test(
        NAME demo-stlc-mcq-${DEMO_NAME}
        COMMAND $<TARGET_FILE:autocog_stlc> --emit ir -I ${PROJECT_SOURCE_DIR}/share/library/stlib ${STL_FILE}
    )
    set_tests_properties(demo-stlc-mcq-${DEMO_NAME} PROPERTIES
        LABELS "stlc;demo"
    )
endforeach()

# ista integration tests: stlc --emit sta | ista --text for MCQ demos
foreach(STL_FILE ${STL_FILES})
    get_filename_component(STL_NAME ${STL_FILE} NAME_WE)
    add_test(NAME demo-ista-mcq-${STL_NAME}
        COMMAND ${CMAKE_COMMAND} -E env
            bash -c "$<TARGET_FILE:autocog_stlc> --emit sta -I ${PROJECT_SOURCE_DIR}/share/library/stlib ${STL_FILE} | $<TARGET_FILE:autocog_ista> --text /dev/stdin > /dev/null"
    )
    set_tests_properties(demo-ista-mcq-${STL_NAME} PROPERTIES
        LABELS "ista;demo"
    )
endforeach()
