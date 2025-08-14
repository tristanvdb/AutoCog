cmake_minimum_required(VERSION 3.18)
project(autocog_llama)

# Set C++ standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Find required packages
find_package(pybind11 REQUIRED)

# Source files
set(SOURCES
    fta.cxx
    ftt.cxx
    model.cxx
    manager.cxx
    evaluation.cxx
    evaluation-choice.cxx
    evaluation-completion.cxx
    evaluation-text.cxx
    python_bindings.cxx
)

# Create the Python module
pybind11_add_module(autocog_llama ${SOURCES})

set_target_properties(autocog_llama PROPERTIES OUTPUT_NAME "llama")

# Include directories
target_include_directories(autocog_llama PRIVATE ../.. )

# Link against llama.cpp
target_link_libraries(autocog_llama PRIVATE llama)

# Compiler flags
target_compile_features(autocog_llama PRIVATE cxx_std_17)

#target_compile_options(autocog_llama PRIVATE
#    $<$<CXX_COMPILER_ID:GNU,Clang>:-O3 -march=native -g>
#    $<$<CXX_COMPILER_ID:MSVC>:/O2>
#)
#target_compile_options(autocog_llama PRIVATE -O0 -g3 -ggdb -fno-omit-frame-pointer -DVERBOSE=1)
target_compile_options(autocog_llama PRIVATE -O3 -DVERBOSE=1)
#target_compile_options(autocog_llama PRIVATE -O3)

target_compile_options(autocog_llama PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall -Wextra -Wpedantic>
    $<$<CXX_COMPILER_ID:MSVC>:/W4>
)

# Python extension properties
set_target_properties(autocog_llama PROPERTIES
    PREFIX "${PYTHON_MODULE_PREFIX}"
    SUFFIX "${PYTHON_MODULE_EXTENSION}"
)

# Installation
install(TARGETS autocog_llama DESTINATION autocog)

