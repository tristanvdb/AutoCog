cmake_minimum_required(VERSION 3.18)
project(autocog)
  
# Set C++ standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE "Release" CACHE STRING "Build type" FORCE)
endif()
set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS "Debug" "Release" "MinSizeRel" "RelWithDebInfo")

if(CMAKE_BUILD_TYPE STREQUAL "Debug")
  add_compile_definitions(VERBOSE=1)
  add_compile_definitions(DEBUG=1)
else()
  add_compile_definitions(VERBOSE=0)
  add_compile_definitions(NDEBUG=1)
endif()

set(COMMON_CXX_FLAGS "-Wall -Wextra -Werror")
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  set(CMAKE_CXX_FLAGS_DEBUG "-g  -O0 -rdynamic -fno-omit-frame-pointer -Wl,--wrap=__cxa_throw ${COMMON_CXX_FLAGS}")
  set(CMAKE_CXX_FLAGS_RELEASE "-O3 -DNDEBUG ${COMMON_CXX_FLAGS}")
endif()

find_package(Python COMPONENTS Interpreter Development REQUIRED)
find_package(pybind11 QUIET)
if(NOT pybind11_FOUND)
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
find_path(REFLEX_INC
  NAMES reflex/flexlexer.h
  HINTS /opt/include /usr/local/include
  REQUIRED
)

find_library(LLAMA_LIB 
  NAMES llama
  HINTS /opt/lib /opt/lib64 /usr/local/lib /usr/local/lib64
  REQUIRED
)

find_path(LLAMA_INC
  NAMES llama.h
  HINTS /opt/include /usr/local/include
  REQUIRED
)

enable_testing()

add_subdirectory(libs/autocog/utilities)

add_subdirectory(libs/autocog/compiler/stl)
add_subdirectory(tools/stlc)
add_subdirectory(bindings/compiler-stl)

add_subdirectory(libs/autocog/llama/xfta)
add_subdirectory(tools/xfta)
add_subdirectory(bindings/llama-xfta)

add_subdirectory(tests/units)

add_subdirectory(share/demos)

