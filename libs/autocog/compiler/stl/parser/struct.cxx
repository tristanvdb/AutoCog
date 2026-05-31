
#include "autocog/compiler/stl/parser.hxx"


namespace autocog::compiler::stl {

template <>
void Parser::parse<ast::Tag::Struct>(ParserState & state, ast::Data<ast::Tag::Struct> & data) {
  state.expect(TokenType::LBRACE, " when starting to parse struct body.");
  while (!state.match(TokenType::RBRACE)) {
    // An inline struct body holds fields plus the constructs that describe them:
    // `search` (policy) and `annotate` (docs). Dispatch on the leading token;
    // anything else is a field. (`define` and prompt-only constructs are not
    // accepted here — an anonymous struct is not a named scope.)
    if (state.current.type == TokenType::SEARCH) {
      data.constructs.emplace_back(std::in_place_index<1>);
      auto & node = std::get<1>(data.constructs.back());
      parse(state, node);
    } else if (state.current.type == TokenType::ANNOTATE) {
      data.constructs.emplace_back(std::in_place_index<0>);
      auto & node = std::get<0>(data.constructs.back());
      parse(state, node);
    } else {
      data.fields.emplace_back(std::make_unique<ast::Field>());
      auto & field = data.fields.back()->data;
      parse(state, field);
    }
  }
}

}

