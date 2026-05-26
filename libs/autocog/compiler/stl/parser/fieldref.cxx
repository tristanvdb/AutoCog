
#include "autocog/compiler/stl/parser.hxx"

#if VERBOSE
#  include <iostream>
#endif

namespace autocog::compiler::stl {

template <> void Parser::parse<ast::Tag::FieldRef>(ParserState & state, ast::Data<ast::Tag::FieldRef> & fldref) {
  if (!state.match(TokenType::DOT)) {
    fldref.prompt.emplace();
    parse(state, fldref.prompt.value().data);
    state.expect(TokenType::DOT, " after prompt in field reference.");
  }
  parse(state, fldref.field.data);
}

}

