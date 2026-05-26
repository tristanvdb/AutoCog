
#include "autocog/compiler/stl/parser.hxx"

#if VERBOSE
#  include <iostream>
#endif

namespace autocog::compiler::stl {

#define DEBUG_Parser_Import VERBOSE && 1

template <>
void Parser::parse<ast::Tag::Import>(ParserState & state, ast::Data<ast::Tag::Import> & import) {
#if DEBUG_Parser_Import
  std::cerr << "Parser::parse<ast::Tag::Import>" << std::endl;
#endif
  state.expect(TokenType::FROM, "when parsing Import statement.");
  state.expect(TokenType::STRING_LITERAL, "when parsing import file path.");
  
  std::string raw_text = state.previous.text;
  
  // Reject f-strings for import paths
  if (!raw_text.empty() && (raw_text[0] == 'f' || raw_text[0] == 'F')) {
    state.throw_error("Format strings (f-strings) are not allowed for import paths.");
  }
  
  // Strip quotes from regular string
  std::string file_path = raw_text;
  if (file_path.length() >= 2 &&
      ((file_path.front() == '"' && file_path.back() == '"') ||
       (file_path.front() == '\'' && file_path.back() == '\''))) {
    file_path = file_path.substr(1, file_path.length() - 2);
  }
  import.file = file_path;
  
  state.expect(TokenType::IMPORT, " after file path in import statement.");
  
  do {
    import.targets.emplace_back();
#if DEBUG_Parser_Import
    std::cerr << "  alias #" << import.targets.size() << std::endl;
#endif
    parse_with_location(state, import.targets.back());
  } while (state.match(TokenType::COMMA));
  
  state.expect(TokenType::SEMICOLON, " to end import statement.");
}

}

