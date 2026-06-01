
#include "autocog/compiler/stl/parser.hxx"


namespace autocog::compiler::stl {

template <>
void Parser::parse<ast::Tag::Assign>(ParserState & state, ast::Data<ast::Tag::Assign> & data) {
  // The key is normally an identifier, but `vocab` is a keyword token; allow it
  // here so `text<vocab=<expr>>` parses as a kwarg whose value is a vocab
  // expression.
  if (state.match(TokenType::VOCAB)) {
    data.argument.data.name = "vocab";
  } else {
    parse(state, data.argument);
  }
  state.expect(TokenType::EQUAL, "to assign value to argument.");
  parse(state, data.value);
}

}

