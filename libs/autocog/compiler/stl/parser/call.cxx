
#include "autocog/compiler/stl/parser.hxx"


namespace autocog::compiler::stl {

template <>
void Parser::parse<ast::Tag::Call>(ParserState & state, ast::Data<ast::Tag::Call> & call) {
  parse(state, call.entry.data);

  // `call f { name use ...; }` names each argument; `call f <path>` spreads the
  // record at <path> as the arguments instead (`_` being the root). The spread
  // form trades a checkable argument list for brevity: nothing here can know
  // what the callee accepts.
  //
  // The two compose: `call f _ { k use ... }` spreads AND names. A resolver
  // that needs one thing the frame does not carry -- the rendering's index
  // base, say -- otherwise had to give up the spread entirely and list every
  // field by hand, which is the same checkability it never had plus a list
  // that goes stale when the schema changes. Named arguments win on a
  // collision: the frame is the default, the binding is the override.
  if (!state.match(TokenType::LBRACE)) {
    call.spread.emplace();
    parse(state, call.spread.value().data);
    if (!state.match(TokenType::LBRACE)) return;
  }

  while (!state.match(TokenType::RBRACE)) {
    call.arguments.emplace_back();
    auto & argument = call.arguments.back().data;
    parse(state, argument);
  }
}

}

