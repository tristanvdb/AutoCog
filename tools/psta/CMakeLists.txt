
add_executable(autocog_psta
  main.cxx
  ${PROJECT_SOURCE_DIR}/libs/autocog/logging.cxx
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
if(SKBUILD_SCRIPTS_DIR)
  install(TARGETS autocog_psta DESTINATION ${SKBUILD_SCRIPTS_DIR})
else()
  install(TARGETS autocog_psta DESTINATION bin)
endif()

add_test(NAME psta_smoke_test
         COMMAND psta --help)
set_tests_properties(psta_smoke_test PROPERTIES
    LABELS "smoke;psta"
)
