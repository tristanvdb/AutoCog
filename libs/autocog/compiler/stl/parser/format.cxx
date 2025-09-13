
#include "autocog/compiler/stl/parser.hxx"

#if VERBOSE
#  include <iostream>
#endif

namespace autocog::compiler::stl {

template <>
void Parser::parse<ast::Tag::Format>(ParserState & state, ast::Data<ast::Tag::Format> & data) {
  switch (state.current.type) {
    case TokenType::IDENTIFIER: {
      state.advance();
      data.type.emplace<0>();
      auto & type = std::get<0>(data.type).data;
      type.name = state.previous.text;
      break;
    }
    case TokenType::TEXT: {
      state.advance();
      data.type.emplace<1>();
      [[maybe_unused]] auto & type = std::get<1>(data.type).data;
      if (state.match(TokenType::LPAREN)) {
        // TODO how should we represent vocab source???
        state.expect(TokenType::RPAREN, ".");
      }
      break;
    }
    case TokenType::ENUM: {
      state.advance();
      data.type.emplace<2>();
      auto & type = std::get<2>(data.type).data;
      state.expect(TokenType::LPAREN, "");
      state.expect(TokenType::STRING_LITERAL, " when parsing enumerators.");
      type.enumerators.emplace_back(state.previous.text);
      while (!state.match(TokenType::RPAREN)) {
        state.expect(TokenType::COMMA, " when parsing enumerators.");
        state.expect(TokenType::STRING_LITERAL, " when parsing enumerators.");
        type.enumerators.emplace_back(state.previous.text);     
      }
      break;
    }
    case TokenType::REPEAT:
    case TokenType::SELECT: {
      state.advance();
      data.type.emplace<3>();
      auto & type = std::get<3>(data.type).data;
      if (state.current.type == TokenType::REPEAT) {
        type.mode = ast::ChoiceKind::Repeat;
      } else {
        type.mode = ast::ChoiceKind::Select;
      }

      state.expect(TokenType::LPAREN, " when parsing a choice format.");
      parse(state, type.source.data);
      state.expect(TokenType::RPAREN, " at the end of a choice format.");
        
      break;
    }
    default: {
      std::ostringstream oss;
      oss << "Unexpected token `" << token_type_name(state.current.type) << "` while parsing top level statements of a Record.";
      state.throw_error(oss.str());
    }
  }
  if (state.match(TokenType::LT)) {
    do {
      auto start = state.current.location;
      state.expect(TokenType::IDENTIFIER, " when parsing format argument name.");
      std::string arg_name = state.previous.text;
      state.expect(TokenType::EQUAL, " when parsing format argument.");
      parse_with_location(state, data.kwargs[arg_name], start);
    } while (state.match(TokenType::COMMA));
    state.expect(TokenType::GT, " to close format arguments.");
  }
  state.expect(TokenType::SEMICOLON, " to finish format statement");
}

}

