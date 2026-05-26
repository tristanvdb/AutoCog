
#include "autocog/compiler/stl/parser.hxx"

#include <filesystem>

#include <stdexcept>

#if VERBOSE
#  include <iostream>
#endif

namespace autocog::compiler::stl {

ParserState::ParserState(
  int fileid,
  std::string const & source_
) :
  stream(source_),
  lexer(stream),
  previous(),
  current(lexer.advance())
{
  lexer.set_file_id(fileid);
}

ParserState::ParserState(
  std::string const & source_
) :
  ParserState(-1, source_)
{}

void ParserState::advance() {
  previous = current;
  current = lexer.advance();
}

bool ParserState::check(TokenType type) {
  return current.type == type;
}

bool ParserState::match(TokenType type) {
  if (check(type)) {
    advance();
    return true;
  } else {
    return false;
  }
}

void ParserState::expect(TokenType type, std::string context) {
  if (!match(type)) {
    std::ostringstream oss;
    oss << "Expected token `" << token_type_name(type) << "` but found `" << token_type_name(current.type) << "` " << context;
    throw_error(oss.str());
  }
}

void ParserState::throw_error(std::string msg) {
  throw ParseError(msg, current.location);
}

}

