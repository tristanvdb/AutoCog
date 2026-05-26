
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
  SourceLocation loc
) :
  message(std::move(msg)),
  location(loc)
{}
  
const char * ParseError::what() const noexcept {
  return message.c_str();
}

Parser::Parser(
  std::list<Diagnostic> & diagnostics_,
  std::unordered_map<std::string, int> & fileids_,
  std::list<std::string> const & search_paths_,
  std::list<ast::Program> & programs_,
  std::list<std::string> const & filepaths
) :
  search_paths(search_paths_),
  diagnostics(diagnostics_),
  fileids(fileids_),
  programs(programs_),
  queue()
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

void queue_imports(ast::Data<ast::Tag::Program> const & program, std::queue<std::string> & queue) {
  for (auto & stmt: program.statements) {
    if (stmt.index() == 0) {
      auto & import = std::get<0>(stmt);
      std::string const & file = import.data.file;
      bool has_stl_extension = file.size() >= 4 && ( file.rfind(".stl") == file.size() - 4 );
      if (has_stl_extension) queue.push(file);
    }
  }
}

#define DEBUG_Parser_parse VERBOSE && 0

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
      queue_imports(programs.back().data, queue);
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

void Parser::parse(int fid, std::string const & name, std::string const & source) {
  ParserState state(fid, source);
  programs.emplace_back(name, fid);
  try {
    parse<ast::Tag::Program>(state, programs.back().data);
  } catch (ParseError const & e) {
    auto line = get_line(source, e.location.line);
    diagnostics.emplace_back(DiagnosticLevel::Error, e.message, line, e.location);
  }
}


bool Parser::parse_fragment(
  std::string const & tag_,
  std::string const & code
) {
  switch (ast::str2tag(tag_)) {
#define X(etag,stag) case ast::Tag::etag: return parse_fragment<ast::Tag::etag>(code);
#include "autocog/compiler/stl/ast/nodes.def"
    default: throw std::runtime_error("Unrecognized ast::Tag!");
  }
}

}
