
#include "autocog/compiler/stl/parser.hxx"

#if VERBOSE
#  include <iostream>
#endif

namespace autocog::compiler::stl {

template <>
void Parser::parse<ast::Tag::Import>(ParserState & state, ast::Data<ast::Tag::Import> & import) {
  state.expect(TokenType::STRING_LITERAL, " when parsing import file path.");
  
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
    state.expect(TokenType::IDENTIFIER, " when parsing import target.");
    std::string target = state.previous.text;
    std::string alias = target;
    if (state.match(TokenType::AS)) {
      state.expect(TokenType::IDENTIFIER, " when parsing import alias.");
      alias = state.previous.text;
    }
    import.targets[alias] = target;
  } while (state.match(TokenType::COMMA));
  
  state.expect(TokenType::SEMICOLON, " to end import statement.");
}

}

