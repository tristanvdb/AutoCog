
add_library(autocog_utilities_lib STATIC
    exception.cxx
)

target_include_directories(autocog_utilities_lib PUBLIC
    ${PROJECT_SOURCE_DIR}/libs
)

set_property(TARGET autocog_utilities_lib PROPERTY POSITION_INDEPENDENT_CODE ON)

