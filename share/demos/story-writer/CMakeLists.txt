
add_test(NAME demo-stlc-story-writer 
         COMMAND stlc -I ${PROJECT_SOURCE_DIR}/share/library -I ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/writer.stl)

