
###################################################################################################
# Binding autocog::backend::llama

pybind11_add_module(autocog_backend_llama_pybind
  module.cxx
)
target_include_directories(autocog_backend_llama_pybind PRIVATE
  ${PROJECT_SOURCE_DIR}/libs
  ${PROJECT_SOURCE_DIR}/vendors/headers
)
target_link_libraries(autocog_backend_llama_pybind PRIVATE
  autocog_backend_llama_lib
  autocog_runtime_store
  autocog_utilities_lib
)
set_property(TARGET autocog_backend_llama_pybind PROPERTY POSITION_INDEPENDENT_CODE ON)
set_target_properties(autocog_backend_llama_pybind PROPERTIES
  OUTPUT_NAME "backend_llama_cxx"
  PREFIX "${PYTHON_MODULE_PREFIX}"
  SUFFIX "${PYTHON_MODULE_EXTENSION}"
)
install(TARGETS autocog_backend_llama_pybind DESTINATION autocog/backend/llama)

add_test(NAME backend_llama_binding_smoke_test
         COMMAND ${Python_EXECUTABLE} -c "import backend_llama_cxx; assert backend_llama_cxx.vocab_size(0) == 258"
         WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
set_tests_properties(backend_llama_binding_smoke_test PROPERTIES
    LABELS "bindings"
    ENVIRONMENT "PYTHONPATH=${CMAKE_CURRENT_BINARY_DIR}")

set_target_properties(autocog_backend_llama_pybind PROPERTIES INSTALL_RPATH "$ORIGIN/../..")
