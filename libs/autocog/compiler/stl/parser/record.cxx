
#include "autocog/compiler/stl/parser.hxx"

#if VERBOSE
#  include <iostream>
#endif

namespace autocog::compiler::stl {

template <>
void Parser::parse<ast::Tag::Record>(ParserState & state, ast::Data<ast::Tag::Record> & data) {
  state.expect(TokenType::LBRACE, " when starting to parse a Record.");
  while (!state.match(TokenType::RBRACE)) {
    auto start = state.current.location;
    switch (state.current.type) {
      case TokenType::DEFINE:
      case TokenType::ARGUMENT: {
        bool is_argument = (state.current.type == TokenType::ARGUMENT);
        state.advance();
        state.expect(TokenType::IDENTIFIER, " when determining defined identifier.");
        auto name = state.previous.text;
        auto & node_ = data.defines[name];
        auto & data_ = node_.data;
        data_.name = name;
        data_.argument = is_argument;
        parse_with_location(state, node_, start);
        break;
      }
      case TokenType::ANNOTATE: {
        state.advance();
        data.annotate.emplace();
        parse_with_location(state, data.annotate.value(), start);
        break;
      }
      case TokenType::SEARCH: {
        state.advance();
        data.search.emplace();
        parse_with_location(state, data.search.value(), start);
        break;
      }
      case TokenType::IS: {
        state.advance();
        if (state.check(TokenType::LBRACE)) {
          data.record.emplace<0>();
          parse_with_location(state, std::get<0>(data.record), start);
        } else {
          data.record.emplace<1>();
          parse_with_location(state, std::get<1>(data.record), start);
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

