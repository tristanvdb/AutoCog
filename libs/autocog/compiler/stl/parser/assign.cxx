
#include "autocog/compiler/stl/parser.hxx"

#if VERBOSE
#  include <iostream>
#endif

namespace autocog::compiler::stl {

template <>
void Parser::parse<ast::Tag::Assign>(ParserState & state, ast::Data<ast::Tag::Assign> & data) {
  parse(state, data.argument);
  state.expect(TokenType::EQUAL, "to assign value to argument.");
  parse(state, data.value);
}

}

