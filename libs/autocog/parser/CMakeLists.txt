cmake_minimum_required(VERSION 3.12)
project(autocog_parser)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Python COMPONENTS Interpreter Development REQUIRED)
find_package(pybind11 QUIET)
if(NOT pybind11_FOUND)
    # Get pybind11 from Python
    execute_process(
        COMMAND ${Python_EXECUTABLE} -c "import pybind11; print(pybind11.get_cmake_dir())"
        OUTPUT_VARIABLE pybind11_DIR
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
    )
    if(pybind11_DIR)
        find_package(pybind11 REQUIRED PATHS ${pybind11_DIR})
    else()
        message(FATAL_ERROR "pybind11 not found. Install with: pip install pybind11")
    endif()
endif()

# RE/flex lexer generation
find_program(REFLEX reflex REQUIRED)
find_library(REFLEX_LIB reflex REQUIRED)

# Generate lexer from .l file
add_custom_command(
    OUTPUT ${CMAKE_CURRENT_SOURCE_DIR}/stl/lexer.cxx ${CMAKE_CURRENT_SOURCE_DIR}/stl/lexer.hxx
    COMMAND ${REFLEX} --header-file=${CMAKE_CURRENT_SOURCE_DIR}/stl/lexer.hxx
                      --outfile=${CMAKE_CURRENT_SOURCE_DIR}/stl/lexer.cxx
                      ${CMAKE_CURRENT_SOURCE_DIR}/stl/lexer.l
    DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/stl/lexer.l
    COMMENT "Generating lexer with RE/flex"
)

add_library(autocog_parser_stl_lib STATIC stl/lexer.cxx stl/parser.cxx stl/token.cxx stl/diagnostic.cxx stl/ir/program.cxx)

target_include_directories(autocog_parser_stl_lib PUBLIC ../.. ../../../vendors/headers)
if(REFLEX_LIB)
    target_link_libraries(autocog_parser_stl_lib PUBLIC ${REFLEX_LIB})
else()
    message(FATAL_ERROR "RE/flex library not found, trying to link manually")
#    target_link_libraries(autocog_parser_stl_lib PUBLIC reflex)
endif()
set_property(TARGET autocog_parser_stl_lib PROPERTY POSITION_INDEPENDENT_CODE ON)

# Executable `autocog-parser-stl`

add_executable(autocog_parser_stl_cli stl/main.cxx)
target_link_libraries(autocog_parser_stl_cli PUBLIC autocog_parser_stl_lib)
set_target_properties(autocog_parser_stl_cli PROPERTIES OUTPUT_NAME "autocog-parser-stl")
install(TARGETS autocog_parser_stl_cli DESTINATION bin)

# Python module
#pybind11_add_module(stl_parser_cpp
#    stl/binding.cxx
#)

#target_link_libraries(stl_parser_cpp PRIVATE
#    autocog_parser_stl_lib
#)



# set_property(TARGET stl_parser_cpp PROPERTY POSITION_INDEPENDENT_CODE ON)


# install(TARGETS stl_parser_cpp DESTINATION ${Python_SITEARCH})

