# autocog::codec — JSON and Python conversion for autocog::data artifacts and
# autocog::types values. Isolated here so the core data/runtime libs never pull
# in nlohmann or Python; only these libs (and tools/bindings) do.

add_library(autocog_codec_json STATIC
    metadata-json.cxx vocab-json.cxx path-json.cxx channel-json.cxx
    sta-json.cxx syntax-json.cxx search-json.cxx ftt-json.cxx fta-json.cxx
    document-json.cxx
)
target_include_directories(autocog_codec_json PUBLIC
    ${PROJECT_SOURCE_DIR}/libs ${PROJECT_SOURCE_DIR}/vendors/headers)
target_link_libraries(autocog_codec_json PUBLIC autocog_data_lib)
set_property(TARGET autocog_codec_json PROPERTY POSITION_INDEPENDENT_CODE ON)

add_library(autocog_codec_python STATIC
    metadata-python.cxx vocab-python.cxx path-python.cxx channel-python.cxx
    sta-python.cxx syntax-python.cxx search-python.cxx ftt-python.cxx fta-python.cxx
    document-python.cxx
)
target_include_directories(autocog_codec_python PUBLIC
    ${PROJECT_SOURCE_DIR}/libs ${PROJECT_SOURCE_DIR}/vendors/headers)
target_link_libraries(autocog_codec_python PUBLIC
    autocog_data_lib pybind11::headers Python::Module)
set_property(TARGET autocog_codec_python PROPERTY POSITION_INDEPENDENT_CODE ON)
