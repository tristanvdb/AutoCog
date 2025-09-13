
#include "autocog/compiler/stl/parser.hxx"

#if VERBOSE
#  include <iostream>
#endif

namespace autocog::compiler::stl {

template <>
void Parser::parse<ast::Tag::Channel>(ParserState & state, ast::Data<ast::Tag::Channel> & data) {
  state.expect(TokenType::LBRACE, " when starting to parse channel body.");
  while (!state.match(TokenType::RBRACE)) {
    data.links.emplace_back();
    auto & link = data.links.back().data;
    parse(state, link);
  }
}

}

