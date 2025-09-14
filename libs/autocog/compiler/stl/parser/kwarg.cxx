
#include "autocog/compiler/stl/parser.hxx"

#if VERBOSE
#  include <iostream>
#endif

namespace autocog::compiler::stl {

template <>
void Parser::parse<ast::Tag::Kwarg>(ParserState & state, ast::Data<ast::Tag::Kwarg> & kwarg) {
  state.expect(TokenType::IDENTIFIER, " for argument name.");
  kwarg.name.data.name = state.previous.text;
  if (state.match(TokenType::USE)) {
    kwarg.source.emplace<0>();
    parse(state, std::get<0>(kwarg.source).data);
  } else if (state.match(TokenType::GET)) {
    kwarg.source.emplace<1>();
    parse(state, std::get<1>(kwarg.source).data);
  } else if (state.match(TokenType::IS)) {
    kwarg.source.emplace<2>();
    parse(state, std::get<2>(kwarg.source).data);
  } else {
    state.throw_error("Expected 'use', 'get', or 'is' to define call argument source.");
  }

  while (!state.match(TokenType::SEMICOLON)) {
    switch (state.current.type) {
      case TokenType::BIND:
        kwarg.clauses.emplace_back(std::in_place_index<0>);
        parse(state, std::get<0>(kwarg.clauses.back()).data);
        break;
      case TokenType::RAVEL:
        kwarg.clauses.emplace_back(std::in_place_index<1>);
        parse(state, std::get<1>(kwarg.clauses.back()).data);
        break;
      case TokenType::WRAP:
        kwarg.clauses.emplace_back(std::in_place_index<2>);
        parse(state, std::get<2>(kwarg.clauses.back()).data);
        break;
      case TokenType::PRUNE:
        kwarg.clauses.emplace_back(std::in_place_index<3>);
        parse(state, std::get<3>(kwarg.clauses.back()).data);
        break;
      case TokenType::MAPPED:
        kwarg.clauses.emplace_back(std::in_place_index<4>);
        parse(state, std::get<4>(kwarg.clauses.back()).data);
        break;
      default: {
        std::ostringstream oss;
        oss << "Call channel argument's clauses can only be `bind`, `ravel`, `wrap`, `prune`, or `mapped`. Found " << token_type_name(state.current.type) << "`.";
        state.throw_error(oss.str());
      }
    }
  }
}

}

