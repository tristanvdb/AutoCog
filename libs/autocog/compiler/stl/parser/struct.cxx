
#include "autocog/compiler/stl/parser.hxx"

#if VERBOSE
#  include <iostream>
#endif

namespace autocog::compiler::stl {

template <>
void Parser::parse<ast::Tag::Struct>(ParserState & state, ast::Data<ast::Tag::Struct> & data) {
  state.expect(TokenType::LBRACE, " when starting to parse struct body.");

  while (!state.match(TokenType::RBRACE)) {
    state.expect(TokenType::IDENTIFIER, " when parsing field name.");
    std::string field_name = state.previous.text;

    data.fields.emplace_back(std::make_unique<ast::Field>());
    auto & field = data.fields.back()->data;
    field.name = field_name;
    parse(state, field);
  }
}

}

