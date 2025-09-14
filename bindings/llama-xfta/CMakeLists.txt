
###################################################################################################
# Binding autocog::llama::xfta to autocog.llama.xfta

pybind11_add_module(autocog_llama_xfta_pybind
  module.cxx
  convert.cxx
)
target_include_directories(autocog_llama_xfta_pybind PRIVATE
  ${PROJECT_SOURCE_DIR}/libs
  ${PROJECT_SOURCE_DIR}/vendors/headers
)
target_link_libraries(autocog_llama_xfta_pybind PRIVATE
  autocog_llama_xfta_lib
)
set_property(TARGET autocog_llama_xfta_pybind PROPERTY POSITION_INDEPENDENT_CODE ON)
set_target_properties(autocog_llama_xfta_pybind PROPERTIES
  OUTPUT_NAME "xfta_cxx"
  PREFIX "${PYTHON_MODULE_PREFIX}"
  SUFFIX "${PYTHON_MODULE_EXTENSION}"
)
install(TARGETS autocog_llama_xfta_pybind DESTINATION autocog/llama/xfta)

add_test(NAME python_xfta_binding_smoke_test
         COMMAND ${Python_EXECUTABLE} -c "import autocog.llama.xfta"
         WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
    
set_tests_properties(python_xfta_binding_smoke_test PROPERTIES
    ENVIRONMENT "PYTHONPATH=${CMAKE_CURRENT_BINARY_DIR}:$ENV{PYTHONPATH}")

