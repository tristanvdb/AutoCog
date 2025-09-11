
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
    ast/program.cxx ast/expr.cxx
    diagnostic.cxx
    ${CMAKE_CURRENT_BINARY_DIR}/autocog_compiler_stl_lexer.cxx
)

target_include_directories(autocog_compiler_stl_lib PUBLIC
    ${PROJECT_SOURCE_DIR}/libs
    ${PROJECT_SOURCE_DIR}/vendors/headers
    ${CMAKE_CURRENT_BINARY_DIR}
    ${REFLEX_INC}
)

target_link_libraries(autocog_compiler_stl_lib PUBLIC
    ${REFLEX_LIB}
)

set_property(TARGET autocog_compiler_stl_lib PROPERTY POSITION_INDEPENDENT_CODE ON)

