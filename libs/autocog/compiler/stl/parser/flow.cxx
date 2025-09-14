
#include "autocog/compiler/stl/parser.hxx"

#if VERBOSE
#  include <iostream>
#endif

namespace autocog::compiler::stl {

template <>
void Parser::parse<ast::Tag::Edge>(ParserState & state, ast::Data<ast::Tag::Edge> & edge) {
  parse(state, edge.prompt.data);
  if (state.match(TokenType::LSQUARE)) {
    edge.limit.emplace();
    parse(state, edge.limit.value().data);
    state.expect(TokenType::RSQUARE, "to end trip limit expression.");
  }
  if (state.match(TokenType::AS)) {
    edge.label.emplace();
    parse(state, edge.label.value().data);
  }
  state.expect(TokenType::SEMICOLON, " to end flow-edge statement.");
}

template <>
void Parser::parse<ast::Tag::Flow>(ParserState & state, ast::Data<ast::Tag::Flow> & data) {
  state.expect(TokenType::FLOW, "when parsing Flow statement.");
  if (state.match(TokenType::LBRACE)) {
    data.short_form = false;
    while (!state.match(TokenType::RBRACE)) {
      data.edges.emplace_back();
      auto & edge = data.edges.back().data;
      parse(state, edge);
    }
  } else {
    data.short_form = true;
    data.edges.emplace_back();
    auto & edge = data.edges.back().data;
    parse(state, edge);
  }
}

}

