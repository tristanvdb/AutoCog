
add_library(autocog_runtime_sta_lib STATIC
    instantiate.cxx
    walk.cxx
)

target_include_directories(autocog_runtime_sta_lib PUBLIC
    ${PROJECT_SOURCE_DIR}/libs
    ${PROJECT_SOURCE_DIR}/vendors/headers
)

target_link_libraries(autocog_runtime_sta_lib PUBLIC
    autocog_utilities_lib
    autocog_data_lib
)

set_property(TARGET autocog_runtime_sta_lib PROPERTY POSITION_INDEPENDENT_CODE ON)
