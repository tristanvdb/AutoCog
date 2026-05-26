
add_library(autocog_runtime_frame_lib STATIC
    frame.cxx
)

target_include_directories(autocog_runtime_frame_lib PUBLIC
    ${PROJECT_SOURCE_DIR}/libs
)

target_link_libraries(autocog_runtime_frame_lib PUBLIC
    autocog_runtime_sta_lib
)

set_property(TARGET autocog_runtime_frame_lib PROPERTY POSITION_INDEPENDENT_CODE ON)
