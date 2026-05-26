
# Generate C++ protobuf files (ir.proto is in same directory)
protobuf_generate(
  LANGUAGE cpp
  OUT_VAR PROTO_IR_CXX
  PROTOS ir.proto
  PROTOC_OUT_DIR ${CMAKE_CURRENT_BINARY_DIR}
)
protobuf_generate(
  LANGUAGE python
  OUT_VAR PROTO_IR_PY
  PROTOS ir.proto
  PROTOC_OUT_DIR ${CMAKE_CURRENT_BINARY_DIR}
)
add_custom_target(generate_python_proto ALL
  DEPENDS ${PROTO_IR_PY}
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

add_library(autocog_compiler_stl_lib STATIC
    token.cxx parser.cxx instantiate.cxx
    ast/program.cxx
    diagnostic.cxx
    ${CMAKE_CURRENT_BINARY_DIR}/autocog_compiler_stl_lexer.cxx
    ${PROTO_IR_CXX}
)

target_include_directories(autocog_compiler_stl_lib PUBLIC
    ${PROJECT_SOURCE_DIR}/libs
    ${PROJECT_SOURCE_DIR}/vendors/headers
    ${CMAKE_CURRENT_BINARY_DIR}
    ${Protobuf_INCLUDE_DIRS}
)

target_link_libraries(autocog_compiler_stl_lib PUBLIC
    ${REFLEX_LIB}
    protobuf::libprotobuf
    absl::log_internal_check_op
    absl::statusor
    absl::strings
)

set_property(TARGET autocog_compiler_stl_lib PROPERTY POSITION_INDEPENDENT_CODE ON)

install(FILES ${PROTO_IR_PY} DESTINATION ${PYTHON_MODULE_PREFIX}/autocog/compiler/stl)

