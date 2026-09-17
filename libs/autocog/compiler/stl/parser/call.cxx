
#include "autocog/compiler/stl/parser.hxx"


namespace autocog::compiler::stl {

template <>
void Parser::parse<ast::Tag::Call>(ParserState & state, ast::Data<ast::Tag::Call> & call) {
  parse(state, call.entry.data);

  // `call f { name use ...; }` names each argument; `call f <path>` spreads the
  // record at <path> as the arguments instead (`_` being the root). The spread
  // form trades a checkable argument list for brevity: nothing here can know
  // what the callee accepts.
  if (!state.match(TokenType::LBRACE)) {
    call.spread.emplace();
    parse(state, call.spread.value().data);
    return;
  }

  while (!state.match(TokenType::RBRACE)) {
    call.arguments.emplace_back();
    auto & argument = call.arguments.back().data;
    parse(state, argument);
  }
}

}

