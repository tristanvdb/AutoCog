add_executable(utilities_backtrace_test_driver
    backtrace_driver.cxx
    ${PROJECT_SOURCE_DIR}/libs/autocog/logging.cxx
)
target_include_directories(utilities_backtrace_test_driver PRIVATE
  ${PROJECT_SOURCE_DIR}/libs
  ${PROJECT_SOURCE_DIR}/vendors/headers
)
target_link_libraries(utilities_backtrace_test_driver PUBLIC
  autocog_utilities_lib
)
add_test(
    NAME utilities_backtrace
    COMMAND utilities_backtrace_test_driver
)
set_tests_properties(utilities_backtrace PROPERTIES LABELS "units;utilities")
