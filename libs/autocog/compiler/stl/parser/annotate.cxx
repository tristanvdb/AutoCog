
#include "autocog/compiler/stl/parser.hxx"

#if VERBOSE
#  include <iostream>
#endif

namespace autocog::compiler::stl {

template <>
void Parser::parse<ast::Tag::Annotation>(ParserState & state, ast::Data<ast::Tag::Annotation> & annotation) {
  annotation.path.emplace();
  parse(state, annotation.path.value().data);
  state.expect(TokenType::AS, " in annotation.");
  parse(state, annotation.description.data);
  state.expect(TokenType::SEMICOLON, " to end annotation.");
}

template <>
void Parser::parse<ast::Tag::Annotate>(ParserState & state, ast::Data<ast::Tag::Annotate> & annotate) {
  state.expect(TokenType::ANNOTATE, "when parsing Annotate statement.");
  if (state.match(TokenType::LBRACE)) {
    annotate.single_statement = false;
    while (!state.match(TokenType::RBRACE)) {
      annotate.annotations.emplace_back();
      auto & annotation = annotate.annotations.back().data;
      parse(state, annotation);
    }
  } else {
    annotate.single_statement = true;
    annotate.annotations.emplace_back();
    auto & annotation = annotate.annotations.back().data;

    parse(state, annotation.description.data);
    state.expect(TokenType::SEMICOLON, " when ending single statement annotation.");
  }
}

}

