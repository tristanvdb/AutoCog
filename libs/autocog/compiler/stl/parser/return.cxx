
#include "autocog/compiler/stl/parser.hxx"

#if VERBOSE
#  include <iostream>
#endif

namespace autocog::compiler::stl {

template <>
void Parser::parse<ast::Tag::Retfield>(ParserState & state, ast::Data<ast::Tag::Retfield> & data) {
  if (!state.check(TokenType::USE) && !state.check(TokenType::IS)) {
    data.alias.emplace();
    parse(state, data.alias.value().data);
  }
  if (state.match(TokenType::USE)) {
    data.source.emplace<1>();
    parse(state, std::get<1>(data.source).data);
  } else if (state.match(TokenType::IS)) {
    data.source.emplace<2>();
    parse(state, std::get<2>(data.source).data);
  }

  while (!state.match(TokenType::SEMICOLON)) {
    switch (state.current.type) {
      case TokenType::BIND:
        data.clauses.emplace_back(std::in_place_index<0>);
        parse(state, std::get<0>(data.clauses.back()).data);
        break;
      case TokenType::RAVEL:
        data.clauses.emplace_back(std::in_place_index<1>);
        parse(state, std::get<1>(data.clauses.back()).data);
        break;
      case TokenType::WRAP:
        data.clauses.emplace_back(std::in_place_index<2>);
        parse(state, std::get<2>(data.clauses.back()).data);
        break;
      case TokenType::PRUNE:
        data.clauses.emplace_back(std::in_place_index<3>);
        parse(state, std::get<3>(data.clauses.back()).data);
        break;
      default: {
        std::ostringstream oss;
        oss << "Return clauses can only be `bind`, `ravel`, `wrap`, or `prune`. Found " << token_type_name(state.current.type) << "`.";
        state.throw_error(oss.str());
      }
    }
  }
}

template <>
void Parser::parse<ast::Tag::Return>(ParserState & state, ast::Data<ast::Tag::Return> & data) {
  state.expect(TokenType::RETURN, "when parsing Return statement.");
  if (!state.check(TokenType::LBRACE) && !state.check(TokenType::USE) && !state.check(TokenType::SEMICOLON)) {
    data.label.emplace();
    parse(state, data.label.value().data);
  }

  if (state.check(TokenType::USE)) {
    data.short_form = true;
    data.fields.emplace_back();
    auto & rfld = data.fields.back().data;
    parse(state, rfld);

  } else if (state.match(TokenType::LBRACE)) {
    data.short_form = true;
    while (!state.match(TokenType::RBRACE)) {
      data.fields.emplace_back();
      auto & rfld = data.fields.back().data;
      parse(state, rfld);
    }

  } else {
    state.expect(TokenType::SEMICOLON, " for empty return.");
  }
}

}

