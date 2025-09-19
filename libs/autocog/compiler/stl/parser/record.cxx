
#include "autocog/compiler/stl/parser.hxx"

#if VERBOSE
#  include <iostream>
#endif

namespace autocog::compiler::stl {

template <>
void Parser::parse<ast::Tag::Record>(ParserState & state, ast::Data<ast::Tag::Record> & data) {
  state.expect(TokenType::RECORD, "when starting a Record.");
  parse(state, data.name);
  state.expect(TokenType::LBRACE, "when defining a Record.");
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
      case TokenType::IS: {
        auto start = state.current.location;
        state.advance();
        if (state.check(TokenType::LBRACE)) {
          data.record.emplace<1>();
          parse(state, std::get<1>(data.record), start);
        } else {
          data.record.emplace<2>();
          parse(state, std::get<2>(data.record), start);
        }
        break;
      }
      default: {
        std::ostringstream oss;
        oss << "Unexpected token `" << token_type_name(state.current.type) << "` while parsing top level statements of a Record.";
        state.throw_error(oss.str());
      }
    }
  }
}

}

