
#include "autocog/compiler/stl/parser.hxx"

#if VERBOSE
#  include <iostream>
#endif

namespace autocog::compiler::stl {

template <>
void Parser::parse<ast::Tag::Program>(ParserState & state, ast::Data<ast::Tag::Program> & data) {
  while (!state.match(TokenType::END_OF_FILE)) {
    auto start = state.current.location;
    switch (state.current.type) {
      case TokenType::FROM: {
        data.imports.emplace_back();
        parse_with_location(state, data.imports.back(), start);
        break;
      }
      case TokenType::EXPORT: {
        data.exports.emplace_back();
        parse_with_location(state, data.exports.back(), start);
        break;
      }
      case TokenType::ANNOTATE: {
        data.annotate.emplace();
        parse_with_location(state, data.annotate.value(), start);
        break;
      }
      case TokenType::SEARCH: {
        data.search.emplace();
        parse_with_location(state, data.search.value(), start);
        break;
      }
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
      case TokenType::RECORD: {
        state.advance();
        state.expect(TokenType::IDENTIFIER, " when parsing name of a Record.");
        auto name = state.previous.text;
        auto & node_ = data.records[name];
        auto & data_ = node_.data;
        data_.name = name;
        parse_with_location(state, node_, start);
        break;
      }
      case TokenType::PROMPT: {
        state.advance();
        state.expect(TokenType::IDENTIFIER, " when parsing name of a Prompt.");
        auto name = state.previous.text;
        auto & node_ = data.prompts[name];
        auto & data_ = node_.data;
        data_.name = name;
        parse_with_location(state, node_, start);
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

