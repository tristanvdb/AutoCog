
#include "autocog/compiler/stl/parser.hxx"
#include "autocog/logging.hxx"


namespace autocog::compiler::stl {


template <>
void Parser::parse<ast::Tag::Define>(ParserState & state, ast::Data<ast::Tag::Define> & define) {
  SPDLOG_LOGGER_TRACE(autocog::log(), "Parser::parse<ast::Tag::Define>");
  if (state.match(TokenType::DEFINE)) {
    define.kind = ast::DefineKind::Define;
  } else if (state.match(TokenType::ARGUMENT)) {
    define.kind = ast::DefineKind::Argument;
  } else if (state.match(TokenType::VOCAB)) {
    define.kind = ast::DefineKind::Vocab;
  } else {
    state.throw_error("Expect `define`, `argument`, or `vocab`.");
  }
  parse(state, define.name);
  if (state.match(TokenType::EQUAL)) {
    define.init.emplace();
    parse(state, define.init.value().data);
  } else if (define.kind == ast::DefineKind::Vocab) {
    // define and argument may omit the initializer; a vocab cannot.
    state.throw_error("Expect `=` and a vocab expression for a vocab.");
  }
  state.expect(TokenType::SEMICOLON, " to end define/argument/vocab statement.");
}

}

