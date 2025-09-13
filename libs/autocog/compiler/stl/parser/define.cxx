
#include "autocog/compiler/stl/parser.hxx"

#if VERBOSE
#  include <iostream>
#endif

namespace autocog::compiler::stl {

#define DEBUG_Parser_Define VERBOSE && 0

template <>
void Parser::parse<ast::Tag::Define>(ParserState & state, ast::Data<ast::Tag::Define> & define) {
#if DEBUG_Parser_Define
    std::cerr << "Parser::parse<ast::Tag::Define>" << std::endl;
#endif
  if (state.match(TokenType::EQUAL)) {
    define.init.emplace();
    parse(state, define.init.value().data);
  }
  state.expect(TokenType::SEMICOLON, " to end define/argument statement.");
}

}

