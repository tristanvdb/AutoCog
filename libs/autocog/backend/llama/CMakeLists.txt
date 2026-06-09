
add_library(autocog_backend_llama_lib STATIC
    prepared.cxx
    model.cxx
    manager.cxx
    evaluation.cxx
    evaluation-choice.cxx
    evaluation-completion.cxx
    evaluation-text.cxx
)

target_include_directories(autocog_backend_llama_lib PUBLIC
    ${PROJECT_SOURCE_DIR}/libs
    ${PROJECT_SOURCE_DIR}/vendors/headers
)

target_link_libraries(autocog_backend_llama_lib PUBLIC
    autocog_data_lib
    llama
)

set_property(TARGET autocog_backend_llama_lib PROPERTY POSITION_INDEPENDENT_CODE ON)
