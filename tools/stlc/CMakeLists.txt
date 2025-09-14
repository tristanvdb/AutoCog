
###################################################################################################
# Compiler for the Structured Thoughts Language

add_executable(autocog_stlc
  main.cxx
  convert.cxx
)
target_include_directories(autocog_stlc PRIVATE
  ${PROJECT_SOURCE_DIR}/libs
  ${PROJECT_SOURCE_DIR}/vendors/headers
)
target_link_libraries(autocog_stlc PUBLIC
  autocog_compiler_stl_lib
)
set_target_properties(autocog_stlc PROPERTIES
  OUTPUT_NAME "stlc"
)
install(TARGETS autocog_stlc DESTINATION bin)

add_test(NAME stlc_smoke_test 
         COMMAND stlc -h)
