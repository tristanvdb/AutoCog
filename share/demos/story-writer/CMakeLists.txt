
set(STLIB ${PROJECT_SOURCE_DIR}/share/library/stlib)
set(SYNTAX ${PROJECT_SOURCE_DIR}/share/syntax/default.json)
set(SEARCH ${PROJECT_SOURCE_DIR}/share/search/default.json)

add_test(NAME demo-stlc-story-writer
         COMMAND $<TARGET_FILE:autocog_stlc> --ir /dev/null -I ${STLIB} -I ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/writer.stl
        )
set_tests_properties(demo-stlc-story-writer PROPERTIES
    LABELS "stlc;demo"
)


add_test(NAME demo-ista-story-writer
    COMMAND ${CMAKE_COMMAND} -E env
        bash -c "
            set -e
            STA=\$(mktemp)
            $<TARGET_FILE:autocog_stlc> --sta \$STA -I ${STLIB} -I ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/writer.stl 2>/dev/null
            $<TARGET_FILE:autocog_ista> --sta \$STA --prompt init_idea --syntax ${SYNTAX} --search ${SEARCH} --content {} --fta /dev/null
            rm -f \$STA
        "
)
set_tests_properties(demo-ista-story-writer PROPERTIES
    LABELS "ista;demo"
)
