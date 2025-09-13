
#include "autocog/compiler/stl/parser.hxx"

#if VERBOSE
#  include <iostream>
#endif

namespace autocog::compiler::stl {

template <>
void Parser::parse<ast::Tag::Search>(ParserState & state, ast::Data<ast::Tag::Search> & search) {
  state.expect(TokenType::LBRACE, " when starting to parse a block of search parameters.");
  while (!state.match(TokenType::RBRACE)) {
    search.params.emplace_back();
    auto & data = search.params.back().data;
    while (state.match(TokenType::IDENTIFIER)) {
      data.locator.emplace_back(state.previous.text);
      if (!state.match(TokenType::DOT)) break;
    }
    state.expect(TokenType::EQUAL, " to set value of search parameter.");
    parse(state, data.value.data);
    state.expect(TokenType::SEMICOLON, " to finish search parameter assignment.");
  }
}

}

