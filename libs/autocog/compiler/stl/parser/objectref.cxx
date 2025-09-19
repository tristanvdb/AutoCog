
#include "autocog/compiler/stl/parser.hxx"

#if VERBOSE
#  include <iostream>
#endif

namespace autocog::compiler::stl {

template <> void Parser::parse<ast::Tag::ObjectRef>(ParserState & state, ast::Data<ast::Tag::ObjectRef> & pref) {
  parse(state, pref.name);
  if (state.match(TokenType::LT)) {
    if (!state.check(TokenType::GT)) {
      do {
        pref.config.emplace_back();
        parse(state, pref.config.back());
      } while (state.match(TokenType::COMMA));
    }
    state.expect(TokenType::GT, "to close compile arguments.");
  }
}

}

