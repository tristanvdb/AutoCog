
#include "autocog/compiler/stl/parser.hxx"

#if VERBOSE
#  include <iostream>
#endif

namespace autocog::compiler::stl {

template <> void Parser::parse<ast::Tag::PromptRef>(ParserState & state, ast::Data<ast::Tag::PromptRef> & pref) {
  state.expect(TokenType::IDENTIFIER, "for callee name.");
  pref.name.data.name = state.previous.text;
  if (state.match(TokenType::LT)) {
    if (!state.check(TokenType::GT)) {
      do {
        state.expect(TokenType::IDENTIFIER, "for callee configuration argument name");
        auto argname = state.previous.text;
        state.expect(TokenType::EQUAL, "for callee configuration argument assignment");
        parse(state, pref.config[argname].data);
      } while (state.match(TokenType::COMMA));
    }
    state.expect(TokenType::GT, "to close compile arguments.");
  }
}

}

