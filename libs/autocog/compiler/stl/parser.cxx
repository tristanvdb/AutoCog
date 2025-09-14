
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
  programs.emplace(name, name);
  try {
    parse<ast::Tag::Program>(state, programs[name].data);
  } catch (ParseError const & e) {
    auto line = get_line(source, e.location.line);
    diagnostics.emplace_back(DiagnosticLevel::Error, e.message, line, e.location);
  }
}

Parser::file_to_program_map_t const & Parser::get() const {
  return programs;
}


bool Parser::parse_fragment(
  std::string const & tag_,
  std::string const & code
) {
  switch (ast::tags.at(tag_)) {
    case ast::Tag::Expression: return parse_fragment<ast::Tag::Expression>(code);
    case ast::Tag::Path:       return parse_fragment<ast::Tag::Path>(code);
    case ast::Tag::FieldRef:   return parse_fragment<ast::Tag::FieldRef>(code);
    case ast::Tag::PromptRef:  return parse_fragment<ast::Tag::PromptRef>(code);
    case ast::Tag::Field:      return parse_fragment<ast::Tag::Field>(code);
    case ast::Tag::Struct:     return parse_fragment<ast::Tag::Struct>(code);
    case ast::Tag::Format:     return parse_fragment<ast::Tag::Format>(code);
    case ast::Tag::Enum:       return parse_fragment<ast::Tag::Enum>(code);
    case ast::Tag::Choice:     return parse_fragment<ast::Tag::Choice>(code);
    case ast::Tag::Text:       return parse_fragment<ast::Tag::Text>(code);
    case ast::Tag::Define:     return parse_fragment<ast::Tag::Define>(code);
    case ast::Tag::Annotate:   return parse_fragment<ast::Tag::Annotate>(code);
    case ast::Tag::Annotation: return parse_fragment<ast::Tag::Annotation>(code);
    case ast::Tag::Search:     return parse_fragment<ast::Tag::Search>(code);
    case ast::Tag::Param:      return parse_fragment<ast::Tag::Param>(code);
    case ast::Tag::Channel:    return parse_fragment<ast::Tag::Channel>(code);
    case ast::Tag::Link:       return parse_fragment<ast::Tag::Link>(code);
    case ast::Tag::Call:       return parse_fragment<ast::Tag::Call>(code);
    case ast::Tag::Kwarg:      return parse_fragment<ast::Tag::Kwarg>(code);
    case ast::Tag::Bind:       return parse_fragment<ast::Tag::Bind>(code);
    case ast::Tag::Ravel:      return parse_fragment<ast::Tag::Ravel>(code);
    case ast::Tag::Mapped:     return parse_fragment<ast::Tag::Mapped>(code);
    case ast::Tag::Wrap:       return parse_fragment<ast::Tag::Wrap>(code);
    case ast::Tag::Prune:      return parse_fragment<ast::Tag::Prune>(code);
    case ast::Tag::Flow:       return parse_fragment<ast::Tag::Flow>(code);
    case ast::Tag::Edge:       return parse_fragment<ast::Tag::Edge>(code);
    case ast::Tag::Return:     return parse_fragment<ast::Tag::Return>(code);
    case ast::Tag::Retfield:   return parse_fragment<ast::Tag::Retfield>(code);
    case ast::Tag::Import:     return parse_fragment<ast::Tag::Import>(code);
    case ast::Tag::Export:     return parse_fragment<ast::Tag::Export>(code);
    case ast::Tag::Record:     return parse_fragment<ast::Tag::Record>(code);
    case ast::Tag::Prompt:     return parse_fragment<ast::Tag::Prompt>(code);
    case ast::Tag::Program:    return parse_fragment<ast::Tag::Program>(code);
    default: throw std::runtime_error("Unrecognized ast::Tag!");
  }
}

}
