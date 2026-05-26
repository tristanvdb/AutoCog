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

# Coverage support: cmake -DCOVERAGE=ON -DCMAKE_BUILD_TYPE=Debug
option(COVERAGE "Enable code coverage instrumentation" OFF)
if(COVERAGE)
  add_compile_options(--coverage -fno-inline)
  add_link_options(--coverage)
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

find_package(Reflex REQUIRED)
get_target_property(REFLEX_EXECUTABLE Reflex::Reflex LOCATION)

find_package(llama REQUIRED)

enable_testing()

add_subdirectory(libs/autocog/utilities)

add_subdirectory(libs/autocog/compiler/stl)
add_subdirectory(tools/stlc)
add_subdirectory(bindings/compiler-stl)

add_subdirectory(libs/autocog/runtime/fta)
add_subdirectory(libs/autocog/runtime/sta)
add_subdirectory(libs/autocog/runtime/frame)
add_subdirectory(libs/autocog/runtime/store)
add_subdirectory(bindings/runtime-sta)

add_subdirectory(libs/autocog/backend/llama)
add_subdirectory(tools/xfta)
add_subdirectory(tools/ista)
add_subdirectory(tools/psta)
add_subdirectory(bindings/backend-llama)

add_subdirectory(tests/units/libs/autocog/compiler/stl/parser)
add_subdirectory(tests/integration/tools/stlc/ir)
add_subdirectory(tests/integration/tools/stlc/sta)
add_subdirectory(tests/integration/tools/stlc/emit)
add_subdirectory(tests/integration/tools/stlc/errors)
add_subdirectory(tests/integration/tools/ista)
add_subdirectory(tests/integration/tools/psta)
add_subdirectory(tests/integration/tools/xfta)
add_subdirectory(tests/integration/tools/e2e)
add_subdirectory(tests/integration/bindings)

add_subdirectory(share/demos)

