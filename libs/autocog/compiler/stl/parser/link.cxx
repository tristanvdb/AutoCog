
#include "autocog/compiler/stl/parser.hxx"

#if VERBOSE
#  include <iostream>
#endif

namespace autocog::compiler::stl {

template <>
void Parser::parse<ast::Tag::Link>(ParserState & state, ast::Data<ast::Tag::Link> & link) {
  parse(state, link.target.data);

  if (state.match(TokenType::USE)) {
    link.source.emplace<1>();
    parse(state, std::get<1>(link.source).data);
  } else if (state.match(TokenType::GET)) {
    link.source.emplace<2>();
    parse(state, std::get<2>(link.source).data);
  } else if (state.match(TokenType::IS)) {
    link.source.emplace<3>();
    parse(state, std::get<3>(link.source).data);
  } else if (state.match(TokenType::CALL)) {
    link.source.emplace<4>();
    parse(state, std::get<4>(link.source).data);
  } else {
    state.throw_error("Expected 'use', 'get', 'is' or 'call' to define channel's source.");
  }

  while (!state.match(TokenType::SEMICOLON)) {
    switch (state.current.type) {
      case TokenType::BIND:
        link.clauses.emplace_back(std::in_place_index<0>);
        parse(state, std::get<0>(link.clauses.back()).data);
        break;
      case TokenType::RAVEL:
        link.clauses.emplace_back(std::in_place_index<1>);
        parse(state, std::get<1>(link.clauses.back()).data);
        break;
      case TokenType::PRUNE:
        link.clauses.emplace_back(std::in_place_index<2>);
        parse(state, std::get<2>(link.clauses.back()).data);
        break;
      case TokenType::WRAP:
        link.clauses.emplace_back(std::in_place_index<3>);
        parse(state, std::get<3>(link.clauses.back()).data);
        break;
      default:
        state.throw_error("Channel clauses can only be `bind`, `ravel`, `prune`, or `wrap`");
    }
  }
}

}

