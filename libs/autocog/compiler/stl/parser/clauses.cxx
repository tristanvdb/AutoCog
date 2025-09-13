
#include "autocog/compiler/stl/parser.hxx"

#if VERBOSE
#  include <iostream>
#endif

namespace autocog::compiler::stl {

template <>
void Parser::parse<ast::Tag::Bind>(ParserState & state, ast::Data<ast::Tag::Bind> & clause) {
  state.expect(TokenType::LPAREN, "for bind clause mandatory source argument.");
  parse(state, clause.source.data);
  if (state.match(TokenType::COMMA)) {
    clause.target.emplace();
    parse(state, clause.target.value().data);
  }
  state.expect(TokenType::RPAREN, "to end bind clause arguments.");
}

template <>
void Parser::parse<ast::Tag::Prune>(ParserState & state, ast::Data<ast::Tag::Prune> & clause) {
  state.expect(TokenType::LPAREN, "for prune clause mandatory source argument.");
  parse(state, clause.target.data);
  state.expect(TokenType::RPAREN, "to end prune clause arguments.");
}

template <>
void Parser::parse<ast::Tag::Ravel>(ParserState & state, ast::Data<ast::Tag::Ravel> & clause) {
  if (state.match(TokenType::LPAREN)) {
    clause.depth.emplace();
    parse(state, clause.depth.value().data);
    if (state.match(TokenType::COMMA)) {
      clause.target.emplace();
      parse(state, clause.target.value().data);
    }
    state.expect(TokenType::RPAREN, "to end ravel clause arguments.");
  }
}

template <>
void Parser::parse<ast::Tag::Mapped>(ParserState & state, ast::Data<ast::Tag::Mapped> & clause) {
  if (state.match(TokenType::LPAREN)) {
    clause.target.emplace();
    parse(state, clause.target.value().data);
    state.expect(TokenType::RPAREN, "to end mapped clause arguments.");
  }
}

template <>
void Parser::parse<ast::Tag::Wrap>(ParserState & state, ast::Data<ast::Tag::Wrap> & clause) {
  if (state.match(TokenType::LPAREN)) {
    clause.target.emplace();
    parse(state, clause.target.value().data);
    state.expect(TokenType::RPAREN, "to end wrap clause arguments.");
  }
}

}

