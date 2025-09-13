
#include "autocog/compiler/stl/parser.hxx"
#include "autocog/compiler/stl/diagnostic.hxx"

#include <filesystem>

#include <stdexcept>

#if VERBOSE
#  include <iostream>
#endif

namespace autocog::compiler::stl {

ParseError::ParseError(
  std::string msg,
  std::string line_,
  SourceLocation loc
) :
  message(std::move(msg)),
  line(line_),
  location(loc)
{}
  
const char * ParseError::what() const noexcept {
  return message.c_str();
}

Parser::Parser(
  std::list<Diagnostic> & diagnostics_,
  std::unordered_map<std::string, int> & fileids_,
  std::list<std::string> const & search_paths_
) :
  search_paths(search_paths_),
  diagnostics(diagnostics_),
  fileids(fileids_),
  queue(),
  programs()
{}

Parser::Parser(
  std::list<Diagnostic> & diagnostics_,
  std::unordered_map<std::string, int> & fileids_,
  std::list<std::string> const & search_paths_,
  std::list<std::string> const & filepaths
) :
  Parser(diagnostics_, fileids_, search_paths_)
{
  for (auto & filepath: filepaths) {
    queue.push(filepath);
  }
}

static std::string file_lookup(std::string const & filepath, std::list<std::string> const & search_paths) {
  std::string found_path;
  if (std::filesystem::exists(filepath)) {
    found_path = filepath;
  } else {
    for (auto const & search_path : search_paths) {
      std::filesystem::path full_path = std::filesystem::path(search_path) / filepath;
      if (std::filesystem::exists(full_path)) {
        found_path = full_path.string();
        break;
      }
    }
  }
  return found_path;
}

#define DEBUG_Parser_parse VERBOSE && 1

void Parser::parse() {
#if DEBUG_Parser_parse
  std::cerr << "ENTER Parser::parse()" << std::endl;
#endif
  while (!queue.empty()) {
    std::string filepath = queue.front();
    queue.pop();

#if DEBUG_Parser_parse
    std::cerr << "  filepath = " << filepath << std::endl;
#endif

    if (fileids.find(filepath) != fileids.end()) continue;
    int fid = fileids.size();
    fileids.emplace(filepath, fid);

    std::string found_path = file_lookup(filepath, search_paths);
    if (found_path.empty()) {
      std::ostringstream oss;
      oss << "Cannot find file: `" << filepath << "`";
      diagnostics.emplace_back(DiagnosticLevel::Error, oss.str());
    } else {
      std::ifstream file(found_path);
      std::string source((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
      file.close();

      parse(fid, filepath, source);
      programs[filepath].exec.queue_imports(queue);
    }
  }
#if DEBUG_Parser_parse
  std::cerr << "LEAVE Parser::parse()" << std::endl;
#endif
}

void clean_raw_string(std::string raw_text, ast::Data<ast::Tag::String> & data) {
  if (raw_text.empty()) {
    throw std::runtime_error("Found empty string literal!");
  }
  data.is_format = (raw_text[0] == 'f' || raw_text[0] == 'F');
  if (data.is_format) {
    raw_text = raw_text.substr(1, raw_text.length() - 1);
  }
  if (raw_text.size() < 2) {
    throw std::runtime_error("Found string literal with less than 2 characters but expect to find quotes!");
  }
  if (raw_text[0] != '"' || raw_text[raw_text.length()-1] != '"') {
    throw std::runtime_error("Found string literal without leading and ending quotes!");
  }
  data.value = raw_text.substr(1, raw_text.length() - 2);
}

void Parser::parse(int fid, std::string const & name, std::string const & source) {
  ParserState state(fid, source, diagnostics);
  programs.emplace(name, name);
  try {
    parse<ast::Tag::Program>(state, programs[name].data);
  } catch (ParseError const & e) {
    diagnostics.emplace_back(DiagnosticLevel::Error, e.message, e.line, e.location);
  }
}

Parser::file_to_program_map_t const & Parser::get() const {
  return programs;
}

}
