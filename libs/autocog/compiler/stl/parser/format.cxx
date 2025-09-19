
#include "autocog/compiler/stl/parser.hxx"

#if VERBOSE
#  include <iostream>
#endif

namespace autocog::compiler::stl {

template <>
void Parser::parse<ast::Tag::Enum>(ParserState & state, ast::Data<ast::Tag::Enum> & type) {
  state.expect(TokenType::LPAREN, "");
  state.expect(TokenType::STRING_LITERAL, " when parsing enumerators.");
  type.enumerators.emplace_back(state.previous.text);
  while (!state.match(TokenType::RPAREN)) {
    state.expect(TokenType::COMMA, " when parsing enumerators.");
    state.expect(TokenType::STRING_LITERAL, " when parsing enumerators.");
    type.enumerators.emplace_back(state.previous.text);     
  }
}

template <>
void Parser::parse<ast::Tag::Text>(ParserState & state, [[maybe_unused]] ast::Data<ast::Tag::Text> & type) {
  if (state.match(TokenType::LPAREN)) {
    // TODO how should we represent vocab source???
    state.expect(TokenType::RPAREN, ".");
  }
}

template <>
void Parser::parse<ast::Tag::Choice>(ParserState & state, ast::Data<ast::Tag::Choice> & type) {
  if (state.current.type == TokenType::REPEAT) {
    type.mode = ast::ChoiceKind::Repeat;
  } else {
    type.mode = ast::ChoiceKind::Select;
  }

  state.expect(TokenType::LPAREN, " when parsing a choice format.");
  parse(state, type.source);
  state.expect(TokenType::RPAREN, " at the end of a choice format.");
}

template <>
void Parser::parse<ast::Tag::Format>(ParserState & state, ast::Data<ast::Tag::Format> & data) {
  switch (state.current.type) {
    case TokenType::IDENTIFIER: {
      data.type.emplace<1>();
      parse(state, std::get<1>(data.type));
      break;
    }
    case TokenType::TEXT: {
      state.advance();
      data.type.emplace<2>();
      parse(state, std::get<2>(data.type));
      break;
    }
    case TokenType::ENUM: {
      state.advance();
      data.type.emplace<3>();
      parse(state, std::get<3>(data.type));
      break;
    }
    case TokenType::REPEAT:
    case TokenType::SELECT: {
      state.advance();
      data.type.emplace<4>();
      parse(state, std::get<4>(data.type));
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
      data.kwargs.emplace_back();
      parse(state, data.kwargs.back());
    } while (state.match(TokenType::COMMA));
    state.expect(TokenType::GT, " to close format arguments.");
  }
  state.expect(TokenType::SEMICOLON, " to finish format statement");
}

}

