#ifndef AUTOCOG_PARSER_STL_PARSER_HXX
#define AUTOCOG_PARSER_STL_PARSER_HXX

#include "autocog/parser/stl/token.hxx"
#include "autocog/parser/stl/ir.hxx"

#include <memory>
#include <queue>
#include <list>
#include <fstream>

namespace autocog::parser {

class Lexer;
struct Diagnostic;

class Parser {
  public:
    using Program = NODE(Program);

    std::queue<std::string> queue;

  private:
    std::list<Diagnostic> & diagnostics;
    MAPPED(Program) programs;

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
