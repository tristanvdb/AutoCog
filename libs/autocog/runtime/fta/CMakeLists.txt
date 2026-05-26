
add_library(autocog_runtime_fta_lib STATIC
    fta.cxx
    ftt.cxx
)

target_include_directories(autocog_runtime_fta_lib PUBLIC
    ${PROJECT_SOURCE_DIR}/libs
)

set_property(TARGET autocog_runtime_fta_lib PROPERTY POSITION_INDEPENDENT_CODE ON)
