
add_test(NAME demo-stlc-story-writer
         COMMAND $<TARGET_FILE:autocog_stlc> --emit ir -I ${PROJECT_SOURCE_DIR}/share/library/stlib -I ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/writer.stl
        )
set_tests_properties(demo-stlc-story-writer PROPERTIES
    LABELS "stlc;demo"
)


add_test(NAME demo-ista-story-writer
    COMMAND ${CMAKE_COMMAND} -E env
        bash -c "$<TARGET_FILE:autocog_stlc> --emit sta -I ${PROJECT_SOURCE_DIR}/share/library/stlib -I ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/writer.stl | $<TARGET_FILE:autocog_ista> --text -s ${PROJECT_SOURCE_DIR}/share/syntax/default.json --search ${PROJECT_SOURCE_DIR}/share/search/default.json -p init_idea /dev/stdin > /dev/null"
)
set_tests_properties(demo-ista-story-writer PROPERTIES
    LABELS "ista;demo"
)
