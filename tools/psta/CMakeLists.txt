
add_executable(autocog_psta
  main.cxx
)
target_include_directories(autocog_psta PRIVATE
  ${PROJECT_SOURCE_DIR}/libs
  ${PROJECT_SOURCE_DIR}/vendors/headers
)
target_link_libraries(autocog_psta PUBLIC
  autocog_runtime_sta_lib
  autocog_utilities_lib
)
set_target_properties(autocog_psta PROPERTIES
  OUTPUT_NAME "psta"
)
install(TARGETS autocog_psta DESTINATION bin)

add_test(NAME psta_smoke_test
         COMMAND psta --help)
set_tests_properties(psta_smoke_test PROPERTIES
    LABELS "smoke;psta"
)
