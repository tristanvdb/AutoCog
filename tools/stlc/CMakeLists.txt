
###################################################################################################
# Compiler for the Structured Thoughts Language

add_executable(autocog_stlc
  main.cxx
  ${PROJECT_SOURCE_DIR}/libs/autocog/logging.cxx
  args.cxx
)
target_include_directories(autocog_stlc PRIVATE
  ${PROJECT_SOURCE_DIR}/libs
  ${PROJECT_SOURCE_DIR}/vendors/headers
)
target_link_libraries(autocog_stlc PUBLIC
  autocog_compiler_stl_serialize
)
set_target_properties(autocog_stlc PROPERTIES
  OUTPUT_NAME "stlc"
)
if(SKBUILD_SCRIPTS_DIR)
  install(TARGETS autocog_stlc DESTINATION ${SKBUILD_SCRIPTS_DIR})
else()
  install(TARGETS autocog_stlc DESTINATION bin)
endif()

add_test(NAME stlc_smoke_test 
         COMMAND stlc -h)
set_tests_properties(stlc_smoke_test PROPERTIES
    LABELS "smoke;stlc"
)
