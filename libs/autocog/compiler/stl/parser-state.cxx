
#include "autocog/compiler/stl/parser.hxx"
#include "autocog/compiler/stl/diagnostic.hxx"

#include <filesystem>

#include <stdexcept>

#if VERBOSE
#  include <iostream>
#endif

namespace autocog::compiler::stl {

static std::string get_line(std::string const & source, int line_pos) {
    if (line_pos <= 0) throw std::runtime_error("In get_line(): line number must be greater than 0");
    std::stringstream ss(source);
    int cnt = 0;
    std::string line;
    while (std::getline(ss, line)) {
        cnt++;
        if (cnt == line_pos) return line;
    }
    throw std::runtime_error("In get_line(): line number must be less than the number of lines in the file");
}

ParserState::ParserState(
  int fileid,
  std::string const & source_,
  std::list<Diagnostic> & diagnostics_
) :
  source(source_),
  diagnostics(diagnostics_),
  stream(source),
  lexer(stream),
  previous(),
  current(lexer.advance())
{
  lexer.set_file_id(fileid);
}

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
  auto & loc = current.location;
  auto line = get_line(source, loc.line);
  throw ParseError(msg, line, loc);
}

void ParserState::emit_error(std::string msg) {
  auto & loc = current.location;
  auto line = get_line(source, loc.line);
  diagnostics.emplace_back(DiagnosticLevel::Error, msg, line, loc);
}

}

