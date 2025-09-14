
#include "autocog/compiler/stl/parser.hxx"

#if VERBOSE
#  include <iostream>
#endif

namespace autocog::compiler::stl {

template <>
void Parser::parse<ast::Tag::Param>(ParserState & state, ast::Data<ast::Tag::Param> & param) {
  state.expect(TokenType::IDENTIFIER, "parameter locator starts with an identifier.");
  param.locator.emplace_back(state.previous.text);
  while (state.match(TokenType::DOT)) {
    state.expect(TokenType::IDENTIFIER, "parameter locator needs identifier after '.'.");
    param.locator.emplace_back(state.previous.text);
  }
  state.expect(TokenType::IS, " to set value of search parameter.");
  parse(state, param.value.data);
  state.expect(TokenType::SEMICOLON, " to finish search parameter assignment.");
}

template <>
void Parser::parse<ast::Tag::Search>(ParserState & state, ast::Data<ast::Tag::Search> & search) {
  state.expect(TokenType::SEARCH, "when parsing Search statement.");
  state.expect(TokenType::LBRACE, " when starting to parse a block of search parameters.");
  while (!state.match(TokenType::RBRACE)) {
    search.params.emplace_back();
    parse(state, search.params.back().data);
  }
}

}

