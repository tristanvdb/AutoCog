cmake_minimum_required(VERSION 3.18)
project(autocog)

# Read version from VERSION file
file(READ "${CMAKE_SOURCE_DIR}/VERSION" AUTOCOG_VERSION_RAW)
string(STRIP "${AUTOCOG_VERSION_RAW}" AUTOCOG_VERSION_RAW)
# Strip leading 'v' if present
string(REGEX REPLACE "^v" "" AUTOCOG_VERSION "${AUTOCOG_VERSION_RAW}")
add_compile_definitions(AUTOCOG_VERSION="${AUTOCOG_VERSION}")
add_compile_definitions(AUTOCOG_BUILD_TYPE="${CMAKE_BUILD_TYPE}")
add_compile_definitions(AUTOCOG_COMPILER_ID="${CMAKE_CXX_COMPILER_ID}")
add_compile_definitions(AUTOCOG_COMPILER_VERSION="${CMAKE_CXX_COMPILER_VERSION}")
add_compile_definitions(AUTOCOG_SYSTEM="${CMAKE_SYSTEM_NAME}-${CMAKE_SYSTEM_PROCESSOR}")
  
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

set(COMMON_CXX_FLAGS "-Wall -Wextra")
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  set(CMAKE_CXX_FLAGS_DEBUG "-g  -O0 -rdynamic -fno-omit-frame-pointer ${COMMON_CXX_FLAGS}")
  # --wrap=__cxa_throw applied after vendor subdirectories (see below)
  set(CMAKE_CXX_FLAGS_RELEASE "-O3 -DNDEBUG ${COMMON_CXX_FLAGS}")
endif()

find_package(Python COMPONENTS Interpreter Development.Module REQUIRED)
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

find_package(Reflex QUIET)
if(NOT Reflex_FOUND)
  message(STATUS "Reflex not found — building from vendored submodule")
  set(REFLEX_SOURCE_DIR "${CMAKE_SOURCE_DIR}/vendors/reflex")
  if(NOT EXISTS "${REFLEX_SOURCE_DIR}/CMakeLists.txt")
    message(FATAL_ERROR "Vendored RE-flex not found. Run: git submodule update --init --recursive")
  endif()
  set(CMAKE_POSITION_INDEPENDENT_CODE ON)
  add_subdirectory("${REFLEX_SOURCE_DIR}" "${CMAKE_BINARY_DIR}/_vendor/reflex" EXCLUDE_FROM_ALL)
  # Mark vendored includes as SYSTEM to suppress warnings in consumer code
  get_target_property(_reflex_inc ReflexLibStatic INTERFACE_INCLUDE_DIRECTORIES)
  if(_reflex_inc)
    set_target_properties(ReflexLibStatic PROPERTIES INTERFACE_INCLUDE_DIRECTORIES "")
    target_include_directories(ReflexLibStatic SYSTEM INTERFACE ${_reflex_inc})
  endif()
  # Create namespaced aliases matching installed package
  if(TARGET ReflexLibStatic AND NOT TARGET Reflex::ReflexLibStatic)
    add_library(Reflex::ReflexLibStatic ALIAS ReflexLibStatic)
  endif()
  set(REFLEX_EXECUTABLE "$<TARGET_FILE:Reflex>")
else()
  get_target_property(REFLEX_EXECUTABLE Reflex::Reflex LOCATION)
endif()

find_package(llama QUIET)
if(NOT llama_FOUND)
  message(STATUS "llama.cpp not found — building from vendored submodule")
  set(LLAMA_SOURCE_DIR "${CMAKE_SOURCE_DIR}/vendors/llama")
  if(NOT EXISTS "${LLAMA_SOURCE_DIR}/CMakeLists.txt")
    message(FATAL_ERROR "Vendored llama.cpp not found. Run: git submodule update --init --recursive")
  endif()
  set(CMAKE_POSITION_INDEPENDENT_CODE ON)
  set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
  set(LLAMA_BUILD_COMMON OFF CACHE BOOL "" FORCE)
  set(LLAMA_BUILD_TOOLS OFF CACHE BOOL "" FORCE)
  set(LLAMA_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
  set(LLAMA_BUILD_TESTS OFF CACHE BOOL "" FORCE)
  set(LLAMA_BUILD_APP OFF CACHE BOOL "" FORCE)
  set(LLAMA_BUILD_SERVER OFF CACHE BOOL "" FORCE)
  # Propagate LLAMA_CUDA to GGML_CUDA (modern llama.cpp uses GGML_CUDA)
  if(LLAMA_CUDA)
    set(GGML_CUDA ON CACHE BOOL "Enable CUDA" FORCE)
  elseif(GGML_CUDA)
    set(LLAMA_CUDA ON)  # for our build info
  endif()
  add_subdirectory("${LLAMA_SOURCE_DIR}" "${CMAKE_BINARY_DIR}/_vendor/llama" EXCLUDE_FROM_ALL)
  set(_LLAMA_SOURCE "vendored")
else()
  set(_LLAMA_SOURCE "system")
endif()

# Build info: CUDA and llama source
if(LLAMA_CUDA OR GGML_CUDA)
  add_compile_definitions(AUTOCOG_CUDA=1)
else()
  add_compile_definitions(AUTOCOG_CUDA=0)
endif()
add_compile_definitions(AUTOCOG_LLAMA_SOURCE="${_LLAMA_SOURCE}")

if(GGML_NATIVE OR NOT DEFINED GGML_NATIVE)
  add_compile_definitions(AUTOCOG_NATIVE=1)
else()
  add_compile_definitions(AUTOCOG_NATIVE=0)
endif()

enable_testing()

# Strict flags (after vendor subdirectories to avoid polluting them)
if(NOT CMAKE_BUILD_TYPE STREQUAL "Debug")
  add_compile_options(-Werror)
  # GCC 12 false positive: -Wmaybe-uninitialized on std::variant with
  # std::optional<std::string> members at -O3. Fixed in GCC 13+.
  add_compile_options(-Wno-error=maybe-uninitialized)
endif()
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
  add_link_options(-Wl,--wrap=__cxa_throw)
endif()

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

# Install stdlib alongside the Python package
install(DIRECTORY share/library/stlib
        DESTINATION autocog
        FILES_MATCHING PATTERN "*.stl" PATTERN "*.py"
        PATTERN "__pycache__" EXCLUDE)

# Install default syntax
install(DIRECTORY share/syntax/
        DESTINATION autocog/syntax
        FILES_MATCHING PATTERN "*.json")

# Install search configs
install(DIRECTORY share/search/
        DESTINATION autocog/search
        FILES_MATCHING PATTERN "*.json")

