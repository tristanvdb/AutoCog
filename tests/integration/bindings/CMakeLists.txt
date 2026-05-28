# Integration tests for Python bindings (runs under ctest)

set(BINDING_PYTHONPATH
    "$<TARGET_FILE_DIR:autocog_compiler_stl_pybind>:$<TARGET_FILE_DIR:autocog_runtime_sta_pybind>:$<TARGET_FILE_DIR:autocog_backend_llama_pybind>"
)

add_test(
    NAME bindings_integration
    COMMAND ${Python_EXECUTABLE}
        ${CMAKE_CURRENT_SOURCE_DIR}/test_bindings.py
        ${PROJECT_SOURCE_DIR}/tests/fixtures/stl
        ${PROJECT_SOURCE_DIR}/share/syntax/default.json
        ${PROJECT_SOURCE_DIR}/share/search/default.json
)
set_tests_properties(bindings_integration PROPERTIES
    LABELS "bindings"
    ENVIRONMENT "PYTHONPATH=${BINDING_PYTHONPATH}"
    TIMEOUT 120
)
