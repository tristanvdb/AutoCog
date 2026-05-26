cmake_minimum_required(VERSION 3.18)
project(autocog)

# Set C++ standard
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

find_library(LLAMA_LIB 
    NAMES llama
    HINTS /opt/lib /opt/lib64 /usr/local/lib /usr/local/lib64
    REQUIRED
)

find_path(LLAMA_INCLUDE_DIR
    NAMES llama.h
    HINTS /opt/include /usr/local/include
    REQUIRED
)

add_subdirectory(libs/autocog/compiler/stl)
add_subdirectory(tools/stlc)
add_subdirectory(bindings/compiler-stl)

add_subdirectory(libs/autocog/llama/xfta)
add_subdirectory(tools/xfta)
add_subdirectory(bindings/llama-xfta)

