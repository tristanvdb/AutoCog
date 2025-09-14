
#include "autocog/compiler/stl/parser.hxx"

#if VERBOSE
#  include <iostream>
#endif

namespace autocog::compiler::stl {

template <>
void Parser::parse<ast::Tag::Export>(ParserState & state, ast::Data<ast::Tag::Export> & entry) {
  state.expect(TokenType::EXPORT, "when parsing Export statement.");
  state.expect(TokenType::IDENTIFIER, " when parsing exported entry point.");
  entry.alias = state.previous.text;
  state.expect(TokenType::IS, " in export statement.");
  state.expect(TokenType::IDENTIFIER, " when parsing export target.");
  entry.target = state.previous.text;
  if (state.match(TokenType::LT)) {
    if (!state.check(TokenType::GT)) {
      do {
        state.expect(TokenType::IDENTIFIER, " when parsing export argument name.");
        std::string arg_name = state.previous.text;
        state.expect(TokenType::EQUAL, " when parsing export argument.");
        parse(state, entry.kwargs[arg_name].data);
      } while (state.match(TokenType::COMMA));
    }
    state.expect(TokenType::GT, " to close export arguments.");
  }
  state.expect(TokenType::SEMICOLON, " to end export statement.");
}

}

