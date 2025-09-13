
#include "autocog/compiler/stl/parser.hxx"

#if VERBOSE
#  include <iostream>
#endif

namespace autocog::compiler::stl {

template <>
void Parser::parse<ast::Tag::Call>(ParserState & state, ast::Data<ast::Tag::Call> & call) {
  parse(state, call.entry.data);
  state.expect(TokenType::LBRACE, " to start call arguments.");

  while (!state.match(TokenType::RBRACE)) {
    call.arguments.emplace_back();
    auto & argument = call.arguments.back().data;
    parse(state, argument);
  }
}

}

