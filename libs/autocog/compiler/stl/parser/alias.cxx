
#include "autocog/compiler/stl/parser.hxx"
#include "autocog/logging.hxx"


namespace autocog::compiler::stl {


template <>
void Parser::parse<ast::Tag::Alias>(ParserState & state, ast::Data<ast::Tag::Alias> & data) {
  SPDLOG_LOGGER_TRACE(autocog::log(), "Parser::parse<ast::Tag::Alias>");
  parse_with_location(state, data.target);
  if (state.match(TokenType::AS)) {
  SPDLOG_LOGGER_TRACE(autocog::log(), "  matched AS");
    data.alias.emplace();
    parse_with_location(state, data.alias.value());
  }
}

}

