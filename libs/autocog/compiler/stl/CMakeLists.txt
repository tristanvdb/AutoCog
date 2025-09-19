
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
    driver.cxx diagnostic.cxx
    token.cxx ast.cxx ast/expr.cxx
    parser.cxx parser-state.cxx
    parser/annotate.cxx parser/call.cxx parser/channel.cxx
    parser/define.cxx parser/export.cxx parser/expression.cxx
    parser/field.cxx parser/fieldref.cxx parser/flow.cxx
    parser/format.cxx parser/import.cxx parser/kwarg.cxx
    parser/link.cxx parser/path.cxx parser/program.cxx
    parser/prompt.cxx parser/promptref.cxx parser/record.cxx
    parser/return.cxx parser/search.cxx parser/clauses.cxx
    parser/struct.cxx
    symbol-table.cxx instantiate.cxx
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

