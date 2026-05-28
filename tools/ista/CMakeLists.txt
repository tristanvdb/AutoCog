
###################################################################################################
# Instantiator of Structured Thought Automations

add_executable(autocog_ista
  main.cxx
  ${PROJECT_SOURCE_DIR}/libs/autocog/logging.cxx
)
target_include_directories(autocog_ista PRIVATE
  ${PROJECT_SOURCE_DIR}/libs
  ${PROJECT_SOURCE_DIR}/vendors/headers
)
target_link_libraries(autocog_ista PUBLIC
  autocog_runtime_sta_lib
  autocog_utilities_lib
)
set_target_properties(autocog_ista PROPERTIES
  OUTPUT_NAME "ista"
)
if(SKBUILD_SCRIPTS_DIR)
  install(TARGETS autocog_ista DESTINATION ${SKBUILD_SCRIPTS_DIR})
else()
  install(TARGETS autocog_ista DESTINATION bin)
endif()

add_test(NAME ista_smoke_test
         COMMAND ista --help)
set_tests_properties(ista_smoke_test PROPERTIES
    LABELS "smoke;ista"
)
