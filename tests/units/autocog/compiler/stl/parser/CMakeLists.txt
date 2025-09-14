
add_executable(stl_parser_test_driver
    driver.cxx
)
target_include_directories(stl_parser_test_driver PRIVATE
  ${PROJECT_SOURCE_DIR}/libs
  ${PROJECT_SOURCE_DIR}/vendors/headers
)
target_link_libraries(stl_parser_test_driver PUBLIC
  autocog_compiler_stl_lib
)

file(GLOB TEST_JSON_FILES ${CMAKE_CURRENT_SOURCE_DIR}/inputs/*.json)

foreach(TEST_FILE ${TEST_JSON_FILES})
    get_filename_component(TEST_NAME ${TEST_FILE} NAME_WE)
    add_test(
        NAME stl_parser_${TEST_NAME}
        COMMAND stl_parser_test_driver ${TEST_FILE}
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    )
endforeach()

set_tests_properties(${TEST_NAMES} PROPERTIES
    REQUIRED_FILES "$<TARGET_FILE:stl_parser_test_driver>"
)

