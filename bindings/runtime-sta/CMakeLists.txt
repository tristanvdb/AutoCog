
###################################################################################################
# Binding autocog::runtime::sta

pybind11_add_module(autocog_runtime_sta_pybind
  module.cxx
)
target_include_directories(autocog_runtime_sta_pybind PRIVATE
  ${PROJECT_SOURCE_DIR}/libs
  ${PROJECT_SOURCE_DIR}/vendors/headers
)
target_link_libraries(autocog_runtime_sta_pybind PRIVATE
  autocog_runtime_sta_lib
  autocog_runtime_store
  autocog_utilities_lib
)
set_property(TARGET autocog_runtime_sta_pybind PROPERTY POSITION_INDEPENDENT_CODE ON)
set_target_properties(autocog_runtime_sta_pybind PROPERTIES
  OUTPUT_NAME "runtime_sta_cxx"
  PREFIX "${PYTHON_MODULE_PREFIX}"
  SUFFIX "${PYTHON_MODULE_EXTENSION}"
)
install(TARGETS autocog_runtime_sta_pybind DESTINATION autocog/runtime/sta)

add_test(NAME runtime_sta_binding_smoke_test
         COMMAND ${Python_EXECUTABLE} -c "import runtime_sta_cxx"
         WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
set_tests_properties(runtime_sta_binding_smoke_test PROPERTIES
    LABELS "bindings"
    ENVIRONMENT "PYTHONPATH=${CMAKE_CURRENT_BINARY_DIR}")

set_target_properties(autocog_runtime_sta_pybind PROPERTIES INSTALL_RPATH "$ORIGIN/../..")
