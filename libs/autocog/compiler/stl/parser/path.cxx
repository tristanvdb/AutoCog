
#include "autocog/compiler/stl/parser.hxx"

#if VERBOSE
#  include <iostream>
#endif

namespace autocog::compiler::stl {

template <>
void Parser::parse<ast::Tag::Path>(ParserState & state, ast::Data<ast::Tag::Path> & path) {
  do {
    state.expect(TokenType::IDENTIFIER, " when parsing path field.");
    
    path.steps.emplace_back();
    auto & step = path.steps.back().data;
    step.field.data.name = state.previous.text;
    step.is_range = false;
    if (state.match(TokenType::LSQUARE)) {
      if (!state.check(TokenType::COLON)) {
        step.lower.emplace();
        parse(state, step.lower.value().data);
      }

      if (state.match(TokenType::COLON)) {
        step.is_range = true;
        if (!state.check(TokenType::RSQUARE)) {
          step.upper.emplace();
          parse(state, step.upper.value().data);
        }
      }

      state.expect(TokenType::RSQUARE, " to close array access.");
    }
  } while (state.match(TokenType::DOT));
}

}

