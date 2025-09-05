
###################################################################################################
# Binding autocog::llama::xfta to autocog.llama.xfta

pybind11_add_module(autocog_llama_xfta_pybind
  module.cxx
  convert.cxx
)
target_include_directories(autocog_llama_xfta_pybind PRIVATE
  ../../libs
  ../../vendors/headers
)
target_link_libraries(autocog_llama_xfta_pybind PRIVATE
  autocog_llama_xfta_lib
)
set_property(TARGET autocog_llama_xfta_pybind PROPERTY POSITION_INDEPENDENT_CODE ON)
set_target_properties(autocog_llama_xfta_pybind PROPERTIES
  OUTPUT_NAME "xfta"
  PREFIX "${PYTHON_MODULE_PREFIX}"
  SUFFIX "${PYTHON_MODULE_EXTENSION}"
)
install(TARGETS autocog_llama_xfta_pybind DESTINATION autocog/llama)

