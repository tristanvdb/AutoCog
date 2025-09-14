
###################################################################################################
# Executor of Finite Thoughts Automations

add_executable(autocog_xfta
  main.cxx
  convert.cxx
)
target_include_directories(autocog_xfta PRIVATE
  ${PROJECT_SOURCE_DIR}/libs
  ${PROJECT_SOURCE_DIR}/vendors/headers
)
target_link_libraries(autocog_xfta PUBLIC
  autocog_llama_xfta_lib
)
set_target_properties(autocog_xfta PROPERTIES
  OUTPUT_NAME "xfta"
)
install(TARGETS autocog_xfta DESTINATION bin)

add_test(NAME xfta_smoke_test 
         COMMAND xfta --help)

