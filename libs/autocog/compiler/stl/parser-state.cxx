
#include "autocog/compiler/stl/parser.hxx"

#include <filesystem>

#include <stdexcept>


namespace autocog::compiler::stl {

ParserState::ParserState(
  int fileid,
  std::string const & source_
) :
  stream(source_),
  lexer(stream),
  previous(),
  current()
{
  // Set the file id before advancing: the first token's location must carry
  // the correct fid (otherwise it defaults to -1, which no fileids entry maps
  // to). This matters for diagnostics anchored at the start of a file, such as
  // an unresolved import whose statement begins on the first token.
  lexer.set_file_id(fileid);
  current = lexer.advance();
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

