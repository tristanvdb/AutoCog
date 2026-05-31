
#include "autocog/compiler/stl/parser.hxx"


namespace autocog::compiler::stl {

template <>
void Parser::parse<ast::Tag::Param>(ParserState & state, ast::Data<ast::Tag::Param> & param) {
  // A locator segment may be any identifier OR keyword: category names like
  // `text`, `enum`, `flow` collide with reserved type/statement tokens, but in
  // a search locator they are plain path segments. Accept the current token's
  // text whatever its kind, except structural delimiters that cannot be names.
  auto take_segment = [&](char const * what) {
    if (state.current.type == TokenType::DOT ||
        state.current.type == TokenType::IS ||
        state.current.type == TokenType::SEMICOLON ||
        state.current.type == TokenType::LBRACE ||
        state.current.type == TokenType::RBRACE ||
        state.current.type == TokenType::END_OF_FILE) {
      state.throw_error(std::string(what));
      return;
    }
    state.advance();
    param.locator.emplace_back(state.previous.text);
  };
  take_segment("parameter locator starts with an identifier or keyword.");
  while (state.match(TokenType::DOT)) {
    take_segment("parameter locator needs an identifier or keyword after '.'.");
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

