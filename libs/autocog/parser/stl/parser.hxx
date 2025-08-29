#ifndef AUTOCOG_PARSER_STL_PARSER_HXX
#define AUTOCOG_PARSER_STL_PARSER_HXX

#include "autocog/parser/stl/token.hxx"
#include "autocog/parser/stl/ir.hxx"
#include "autocog/parser/stl/lexer.hxx"

#include <memory>
#include <queue>
#include <list>
#include <fstream>
#include <sstream>

namespace autocog::parser {

class Lexer;
struct Diagnostic;

struct ParserState {
  std::string filename;
  std::string source;
  std::list<Diagnostic> & diagnostics;

  std::istringstream stream;
  Lexer lexer;

  Token previous;
  Token current;
  bool error;

  ParserState(std::string const & filename_, std::string const & source_, std::list<Diagnostic> & diagnostics_);

  void advance();

  bool check(TokenType type);
  bool match(TokenType type);
  bool expect(TokenType type, std::string context);

  void emit_error(std::string msg);
};

class Parser {
  public:
    using Program = NODE(Program);

    std::queue<std::string> queue; //< file queue

  private:
    std::list<Diagnostic> & diagnostics;
    MAPPED(Program) programs;

    template <IrTag tag>
    void parse(ParserState & state, IrData<tag> & data);

  public:
    Parser(std::list<Diagnostic> & diagnostics_);

    void parse();
    void parse(std::string const & filename, std::string const & source);

    MAPPED(Program) const & get() const;

    bool has_prompt(std::string const & filename, std::string const & objname) const;
    NODE(Prompt) const & get_prompt(std::string const & filename, std::string const & objname) const;

    bool has_record(std::string const & filename, std::string const & objname) const;
    NODE(Record) const & get_record(std::string const & filename, std::string const & objname) const;
};

}

#endif // AUTOCOG_PARSER_STL_PARSER_HXX
