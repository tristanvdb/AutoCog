
#include "autocog/compiler/stl/parser.hxx"

#if VERBOSE
#  include <iostream>
#endif

namespace autocog::compiler::stl {

template <>
void Parser::parse<ast::Tag::Field>(ParserState & state, ast::Data<ast::Tag::Field> & field) {
  parse(state, field.name);
  if (state.match(TokenType::LSQUARE)) {
    field.lower.emplace();
    parse(state, field.lower.value());
    
    if (state.match(TokenType::COLON)) {
      field.upper.emplace();
      parse(state, field.upper.value());
    }
    state.expect(TokenType::RSQUARE, " to close array dimension.");
  }
  state.expect(TokenType::IS, " between field name and type.");
  if (state.check(TokenType::LBRACE)) {
    field.type.emplace<2>();
    parse(state, std::get<2>(field.type));
  } else {
    field.type.emplace<1>();
    parse(state, std::get<1>(field.type));
  }
}

}

