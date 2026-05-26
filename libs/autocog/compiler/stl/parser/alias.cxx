
#include "autocog/compiler/stl/parser.hxx"

#if VERBOSE
#  include <iostream>
#endif

namespace autocog::compiler::stl {

#define DEBUG_Parser_Alias VERBOSE && 0

template <>
void Parser::parse<ast::Tag::Alias>(ParserState & state, ast::Data<ast::Tag::Alias> & data) {
#if DEBUG_Parser_Alias
  std::cerr << "Parser::parse<ast::Tag::Alias>" << std::endl;
#endif
  parse_with_location(state, data.target);
  if (state.match(TokenType::AS)) {
#if DEBUG_Parser_Alias
    std::cerr << "  matched AS" << std::endl;
#endif
    data.alias.emplace();
    parse_with_location(state, data.alias.value());
  }
}

}

