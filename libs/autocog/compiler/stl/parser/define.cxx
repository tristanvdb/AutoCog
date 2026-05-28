
#include "autocog/compiler/stl/parser.hxx"
#include "autocog/logging.hxx"


namespace autocog::compiler::stl {


template <>
void Parser::parse<ast::Tag::Define>(ParserState & state, ast::Data<ast::Tag::Define> & define) {
  SPDLOG_LOGGER_TRACE(autocog::log(), "Parser::parse<ast::Tag::Define>");
  if (state.match(TokenType::DEFINE)) {
    define.is_argument = false;
  } else if (state.match(TokenType::ARGUMENT)) {
    define.is_argument = true;
  } else {
    state.throw_error("Expect either `define` or `argument`.");
  }
  parse(state, define.name);
  if (state.match(TokenType::EQUAL)) {
    define.init.emplace();
    parse(state, define.init.value().data);
  }
  state.expect(TokenType::SEMICOLON, " to end define/argument statement.");
}

}

