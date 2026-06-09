# Unit test for the binding datastore surface (binding only; no autocog package).
set(BINDING_PYTHONPATH
    "$<TARGET_FILE_DIR:autocog_compiler_stl_pybind>:$<TARGET_FILE_DIR:autocog_runtime_sta_pybind>:$<TARGET_FILE_DIR:autocog_backend_llama_pybind>"
)

add_test(
    NAME bindings_store_unit
    COMMAND ${Python_EXECUTABLE}
        ${CMAKE_CURRENT_SOURCE_DIR}/test_store.py
        ${PROJECT_SOURCE_DIR}/share
)
set_tests_properties(bindings_store_unit PROPERTIES
    LABELS "units;bindings"
    ENVIRONMENT "PYTHONPATH=${BINDING_PYTHONPATH}"
    TIMEOUT 120
)
