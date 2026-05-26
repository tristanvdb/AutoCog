
#include "autocog/compiler/stl/parser.hxx"

#if VERBOSE
#  include <iostream>
#endif

namespace autocog::compiler::stl {

template <>
void Parser::parse<ast::Tag::Prompt>(ParserState & state, ast::Data<ast::Tag::Prompt> & data) {
  state.expect(TokenType::PROMPT, "when starting a Prompt.");
  parse(state, data.name);
  state.expect(TokenType::LBRACE, "when defining a Prompt.");
  while (!state.match(TokenType::RBRACE)) {
    switch (state.current.type) {
      case TokenType::DEFINE:
      case TokenType::ARGUMENT: {
        data.constructs.emplace_back(std::in_place_index<0>);
        auto & node = std::get<0>(data.constructs.back());
        parse(state, node);
        break;
      }
      case TokenType::ANNOTATE: {
        data.constructs.emplace_back(std::in_place_index<1>);
        auto & node = std::get<1>(data.constructs.back());
        parse(state, node);
        break;
      }
      case TokenType::SEARCH: {
        data.constructs.emplace_back(std::in_place_index<2>);
        auto & node = std::get<2>(data.constructs.back());
        parse(state, node);
        break;
      }
      case TokenType::ALIAS: {
        auto start = state.current.location;
        data.constructs.emplace_back(std::in_place_index<3>);
        auto & node = std::get<3>(data.constructs.back());
        state.advance();
        parse(state, node, start);
        break;
      }
      case TokenType::CHANNEL: {
        data.constructs.emplace_back(std::in_place_index<4>);
        auto & node = std::get<4>(data.constructs.back());
        parse(state, node);
        break;
      }
      case TokenType::FLOW: {
        data.constructs.emplace_back(std::in_place_index<5>);
        auto & node = std::get<5>(data.constructs.back());
        parse(state, node);
        break;
      }
      case TokenType::RETURN: {
        data.constructs.emplace_back(std::in_place_index<6>);
        auto & node = std::get<6>(data.constructs.back());
        parse(state, node);
        break;
      }
      case TokenType::IS: {
        auto start = state.current.location;
        data.fields.emplace();
        state.advance();
        parse(state, data.fields.value(), start);
        break;
      }
      default: {
        std::ostringstream oss;
        oss << "Unexpected token `" << token_type_name(state.current.type) << "` while parsing top level statements of a Prompt.";
        state.throw_error(oss.str());
      }
    }
  }
}

}

