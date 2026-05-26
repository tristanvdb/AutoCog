
add_library(autocog_llama_xfta_lib STATIC
    fta.cxx
    ftt.cxx
    model.cxx
    manager.cxx
    evaluation.cxx
    evaluation-choice.cxx
    evaluation-completion.cxx
    evaluation-text.cxx
)

target_include_directories(autocog_llama_xfta_lib PUBLIC
    ${PROJECT_SOURCE_DIR}/libs
    ${PROJECT_SOURCE_DIR}/vendors/headers
     ${LLAMA_INCLUDE_DIR}
)

target_link_libraries(autocog_llama_xfta_lib PUBLIC
    ${LLAMA_LIB}
)

set_property(TARGET autocog_llama_xfta_lib PROPERTY POSITION_INDEPENDENT_CODE ON)

