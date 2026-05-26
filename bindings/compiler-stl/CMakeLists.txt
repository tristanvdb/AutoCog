
###################################################################################################
# Binding autocog::compiler::stl to autocog.compiler.stl

pybind11_add_module(autocog_compiler_stl_pybind
  module.cxx
  convert.cxx
)
target_include_directories(autocog_compiler_stl_pybind PRIVATE
  ${PROJECT_SOURCE_DIR}/libs
  ${PROJECT_SOURCE_DIR}/vendors/headers
)
target_link_libraries(autocog_compiler_stl_pybind PRIVATE
  autocog_compiler_stl_lib
  autocog_utilities_lib
)
set_property(TARGET autocog_compiler_stl_pybind PROPERTY POSITION_INDEPENDENT_CODE ON)
set_target_properties(autocog_compiler_stl_pybind PROPERTIES
  OUTPUT_NAME "stl_cxx"
  PREFIX "${PYTHON_MODULE_PREFIX}"
  SUFFIX "${PYTHON_MODULE_EXTENSION}"
)
install(TARGETS autocog_compiler_stl_pybind DESTINATION autocog/compiler/stl)

add_test(NAME python_stlc_binding_smoke_test
         COMMAND ${Python_EXECUTABLE} -c "import autocog.compiler.stl"
         WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
    
set_tests_properties(python_stlc_binding_smoke_test PROPERTIES
    ENVIRONMENT "PYTHONPATH=${CMAKE_CURRENT_BINARY_DIR}:$ENV{PYTHONPATH}")
