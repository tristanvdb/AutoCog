
###################################################################################################
# Binding autocog::compiler::stl to autocog.compiler.stl

pybind11_add_module(autocog_compiler_stl_pybind
  module.cxx
  convert.cxx
)
target_include_directories(autocog_compiler_stl_pybind PRIVATE
  ../../libs
  ../../vendors/headers
)
target_link_libraries(autocog_compiler_stl_pybind PRIVATE
  autocog_compiler_stl_lib
)
set_property(TARGET autocog_compiler_stl_pybind PROPERTY POSITION_INDEPENDENT_CODE ON)
set_target_properties(autocog_compiler_stl_pybind PROPERTIES
  OUTPUT_NAME "stl_cxx"
  PREFIX "${PYTHON_MODULE_PREFIX}"
  SUFFIX "${PYTHON_MODULE_EXTENSION}"
)
install(TARGETS autocog_compiler_stl_pybind DESTINATION autocog/compiler/stl)

