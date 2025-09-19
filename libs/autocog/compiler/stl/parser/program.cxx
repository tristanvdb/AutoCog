
#include "autocog/compiler/stl/parser.hxx"

#if VERBOSE
#  include <iostream>
#endif

namespace autocog::compiler::stl {

template <>
void Parser::parse<ast::Tag::Program>(ParserState & state, ast::Data<ast::Tag::Program> & data) {
  while (!state.match(TokenType::END_OF_FILE)) {
    switch (state.current.type) {
      case TokenType::FROM: {
        data.statements.emplace_back(std::in_place_index<0>);
        auto & node = std::get<0>(data.statements.back());
        parse(state, node);
        break;
      }
      case TokenType::ALIAS:
      case TokenType::EXPORT: {
        auto start = state.current.location;
        data.statements.emplace_back(std::in_place_index<1>);
        auto & node = std::get<1>(data.statements.back());
        node.data.is_export = (state.current.type == TokenType::EXPORT);
        state.advance();
        parse(state, node, start);
        state.expect(TokenType::SEMICOLON, "to end alias/export statements");
        break;
      }
      case TokenType::DEFINE:
      case TokenType::ARGUMENT: {
        data.statements.emplace_back(std::in_place_index<2>);
        auto & node = std::get<2>(data.statements.back());
        parse(state, node);
        break;
      }
      case TokenType::ANNOTATE: {
        data.statements.emplace_back(std::in_place_index<3>);
        auto & node = std::get<3>(data.statements.back());
        parse(state, node);
        break;
      }
      case TokenType::SEARCH: {
        data.statements.emplace_back(std::in_place_index<4>);
        auto & node = std::get<4>(data.statements.back());
        parse(state, node);
        break;
      }
      case TokenType::RECORD: {
        data.statements.emplace_back(std::in_place_index<5>);
        auto & node = std::get<5>(data.statements.back());
        parse(state, node);
        break;
      }
      case TokenType::PROMPT: {
        data.statements.emplace_back(std::in_place_index<6>);
        auto & node = std::get<6>(data.statements.back());
        parse(state, node);
        break;
      }
      default: {
        std::ostringstream oss;
        oss << "Unexpected token `" << token_type_name(state.current.type) << "` while parsing statement of Program.";
        state.throw_error(oss.str());
      }
    }
  }
}

}

