
#include "autocog/compiler/stl/parser.hxx"

#if VERBOSE
#  include <iostream>
#endif

namespace autocog::compiler::stl {

template <>
void Parser::parse<ast::Tag::Annotate>(ParserState & state, ast::Data<ast::Tag::Annotate> & annotate) {
  if (state.match(TokenType::LBRACE)) {
    // Block form: annotate { path as "description"; ... }
    annotate.single_statement = false;
    
    while (!state.match(TokenType::RBRACE)) {
      annotate.annotations.emplace_back();
      auto & annotation = annotate.annotations.back().data;
      
      // Parse optional path (could be _ for no path)
      if (!(state.match(TokenType::IDENTIFIER) && state.previous.text == "_")) {
        annotation.path.emplace();
        parse(state, annotation.path.value().data);
      }
      
      state.expect(TokenType::AS, " in annotation.");
      state.expect(TokenType::STRING_LITERAL, " for annotation description.");
      
      // Handle both regular and format strings
      std::string raw_text = state.previous.text;
      if (!raw_text.empty() && (raw_text[0] == 'f' || raw_text[0] == 'F')) {
        annotation.description.data.is_format = true;
        // Remove f prefix and quotes
        if (raw_text.length() >= 3) {
          annotation.description.data.value = raw_text.substr(2, raw_text.length() - 3);
        }
      } else {
        annotation.description.data.is_format = false;
        // Remove just quotes
        if (raw_text.length() >= 2 &&
            ((raw_text.front() == '"' && raw_text.back() == '"') ||
             (raw_text.front() == '\'' && raw_text.back() == '\''))) {
          annotation.description.data.value = raw_text.substr(1, raw_text.length() - 2);
        } else {
          annotation.description.data.value = raw_text;
        }
      }
      
      state.expect(TokenType::SEMICOLON, " to end annotation.");
    }
  } else {
    state.expect(TokenType::STRING_LITERAL, " when parsing autonomous annotation.");

    // Single statement form
    annotate.single_statement = true;
    annotate.annotations.emplace_back();
    auto & annotation = annotate.annotations.back().data;

    // Handle both regular and format strings
    std::string raw_text = state.previous.text;
    if (!raw_text.empty() && (raw_text[0] == 'f' || raw_text[0] == 'F')) {
      annotation.description.data.is_format = true;
      if (raw_text.length() >= 3) {
        annotation.description.data.value = raw_text.substr(2, raw_text.length() - 3);
      }
    } else {
      annotation.description.data.is_format = false;
      if (raw_text.length() >= 2 &&
          ((raw_text.front() == '"' && raw_text.back() == '"') ||
           (raw_text.front() == '\'' && raw_text.back() == '\''))) {
        annotation.description.data.value = raw_text.substr(1, raw_text.length() - 2);
      } else {
        annotation.description.data.value = raw_text;
      }
    }

    state.expect(TokenType::SEMICOLON, " when ending single statement annotation.");
  }
}

}

