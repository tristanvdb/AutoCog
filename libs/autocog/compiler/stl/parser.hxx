#ifndef AUTOCOG_COMPILER_STL_PARSER_HXX
#define AUTOCOG_COMPILER_STL_PARSER_HXX

#include "autocog/compiler/stl/token.hxx"
#include "autocog/compiler/stl/ast.hxx"

#include "autocog_compiler_stl_lexer.hxx" //< Generated file

#include <memory>
#include <queue>
#include <list>
#include <fstream>
#include <sstream>

namespace autocog::compiler::stl {

class Lexer;
struct Diagnostic;

struct ParserState {
  std::string source;
  std::list<Diagnostic> & diagnostics;

  std::istringstream stream;
  Lexer lexer;

  Token previous;
  Token current;

  ParserState(int fid, std::string const & source_, std::list<Diagnostic> & diagnostics_);

  void advance();

  bool check(TokenType type);
  bool match(TokenType type);
  bool expect(TokenType type, std::string context);

  void emit_error(std::string msg);
};

class Parser {
  public:
    using file_to_program_map_t = std::unordered_map<std::string, ast::Program>;

  private:
    std::list<std::string> const & search_paths;
    std::list<Diagnostic> & diagnostics;
    std::unordered_map<std::string, int> & fileids;
    std::queue<std::string> queue;
    file_to_program_map_t programs;

    template <ast::Tag tag>
    void parse(ParserState & state, ast::Data<tag> & data, int min_precedence);
    template <ast::Tag tag>
    void parse(ParserState & state, ast::Data<tag> & data);

  public:
    Parser(std::list<Diagnostic> &, std::unordered_map<std::string, int> &, std::list<std::string> const &);
    Parser(std::list<Diagnostic> &, std::unordered_map<std::string, int> &, std::list<std::string> const &, std::list<std::string> const &);

    void parse();
    void parse(int fid, std::string const & filename, std::string const & source);

    file_to_program_map_t const & get() const;

    bool has_prompt(std::string const & filename, std::string const & objname) const;
    ast::Prompt const & get_prompt(std::string const & filename, std::string const & objname) const;

    bool has_record(std::string const & filename, std::string const & objname) const;
    ast::Record const & get_record(std::string const & filename, std::string const & objname) const;
};

}

#endif // AUTOCOG_COMPILER_STL_PARSER_HXX
