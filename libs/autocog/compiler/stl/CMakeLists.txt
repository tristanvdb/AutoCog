cmake_minimum_required(VERSION 3.12)
project(autocog_compiler_stl)

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

find_program(REFLEX reflex REQUIRED)
find_library(REFLEX_LIB reflex REQUIRED)
find_package(Protobuf REQUIRED)
find_package(absl REQUIRED)

# Generate C++ protobuf files (ir.proto is in same directory)
protobuf_generate(
  LANGUAGE cpp
  OUT_VAR PROTO_IR_SRCS
  PROTOS ir.proto
  PROTOC_OUT_DIR ${CMAKE_CURRENT_BINARY_DIR}
)

# Generate lexer from .l file
add_custom_command(
    OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/autocog_compiler_stl_lexer.cxx ${CMAKE_CURRENT_BINARY_DIR}/autocog_compiler_stl_lexer.hxx
    COMMAND ${REFLEX} --header-file=${CMAKE_CURRENT_BINARY_DIR}/autocog_compiler_stl_lexer.hxx
                      --outfile=${CMAKE_CURRENT_BINARY_DIR}/autocog_compiler_stl_lexer.cxx
                      ${CMAKE_CURRENT_SOURCE_DIR}/lexer.l
    DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/lexer.l
    COMMENT "Generating lexer with RE/flex"
)

# Sources
add_library(autocog_compiler_stl_lib STATIC
    token.cxx parser.cxx instantiate.cxx
    ast/program.cxx
    diagnostic.cxx
    ${CMAKE_CURRENT_BINARY_DIR}/autocog_compiler_stl_lexer.cxx
    ${PROTO_IR_SRCS}
)

# Header paths
target_include_directories(autocog_compiler_stl_lib PUBLIC
    ../../..
    ${CMAKE_CURRENT_BINARY_DIR}
    ${Protobuf_INCLUDE_DIRS}
    ../../../../vendors/headers
)

# Libraries
target_link_libraries(autocog_compiler_stl_lib PUBLIC
    ${REFLEX_LIB}
    protobuf::libprotobuf
    absl::log_internal_check_op
    absl::statusor
    absl::strings
)

# FPIC
set_property(TARGET autocog_compiler_stl_lib PROPERTY POSITION_INDEPENDENT_CODE ON)

# Executable `autocog-compiler-stl`

add_executable(autocog_compiler_stl_cli main.cxx)
target_link_libraries(autocog_compiler_stl_cli PUBLIC autocog_compiler_stl_lib)
set_target_properties(autocog_compiler_stl_cli PROPERTIES OUTPUT_NAME "autocog-compiler-stl")
install(TARGETS autocog_compiler_stl_cli DESTINATION bin)


# Python module
#pybind11_add_module(stl_compiler_stl_pybind
#    binding.cxx
#)
#target_link_libraries(stl_compiler_stl_pybind PRIVATE
#    autocog_compiler_stl_lib
#)
#set_property(TARGET stl_compiler_stl_pybind PROPERTY POSITION_INDEPENDENT_CODE ON)
#protobuf_generate(
#  LANGUAGE python
#  OUT_VAR PROTO_IR_PY
#  PROTOS ir.proto
#  PROTOC_OUT_DIR ${CMAKE_BINARY_DIR}/python/autocog/compiler
#)
