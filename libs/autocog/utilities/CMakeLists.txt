
add_library(autocog_utilities_lib STATIC
    exception.cxx
)

target_include_directories(autocog_utilities_lib PUBLIC
    ${PROJECT_SOURCE_DIR}/libs
)

target_link_libraries(autocog_utilities_lib PUBLIC ${CMAKE_DL_LIBS})

set_property(TARGET autocog_utilities_lib PROPERTY POSITION_INDEPENDENT_CODE ON)

