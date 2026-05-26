
add_library(autocog_runtime_sta_lib STATIC
    sta.cxx
    load.cxx
    instantiate.cxx
    parse.cxx
)

target_include_directories(autocog_runtime_sta_lib PUBLIC
    ${PROJECT_SOURCE_DIR}/libs
    ${PROJECT_SOURCE_DIR}/vendors/headers
)

set_property(TARGET autocog_runtime_sta_lib PROPERTY POSITION_INDEPENDENT_CODE ON)
