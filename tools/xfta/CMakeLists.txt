
###################################################################################################
# Executor of Finite Thoughts Automations

add_executable(autocog_xfta
  main.cxx
)
target_include_directories(autocog_xfta PRIVATE
  ${PROJECT_SOURCE_DIR}/libs
  ${PROJECT_SOURCE_DIR}/vendors/headers
)
target_link_libraries(autocog_xfta PUBLIC
  autocog_backend_llama_lib
  autocog_utilities_lib
)
set_target_properties(autocog_xfta PROPERTIES
  OUTPUT_NAME "xfta"
)
if(SKBUILD_SCRIPTS_DIR)
  install(TARGETS autocog_xfta DESTINATION ${SKBUILD_SCRIPTS_DIR})
else()
  install(TARGETS autocog_xfta DESTINATION bin)
endif()

add_test(NAME xfta_smoke_test 
         COMMAND xfta --help)
set_tests_properties(xfta_smoke_test PROPERTIES
    LABELS "smoke;xfta"
)

