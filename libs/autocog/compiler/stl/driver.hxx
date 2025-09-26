#ifndef AUTOCOG_COMPILER_STL_DRIVER_HXX
#define AUTOCOG_COMPILER_STL_DRIVER_HXX

#include "autocog/compiler/stl/diagnostic.hxx"
#include "autocog/compiler/stl/ast.hxx"
#include "autocog/compiler/stl/symbol-table.hxx"

#include <list>
#include <string>
#include <variant>
#include <optional>
#include <unordered_map>

namespace autocog::compiler::stl {

class Driver {
  public:
    std::list<std::string> inputs;
    std::list<std::string> includes;
    std::unordered_map<std::string, std::variant<bool,int,float,std::string>> defines;

    std::optional<std::string> output = std::nullopt;
    bool verbose = false;

  private:
    unsigned errors = 0;
    unsigned warnings = 0;
    unsigned notes = 0;
    std::list<Diagnostic> diagnostics;
    std::unordered_map<std::string, int> fileids;

    bool report_errors();
    void emit_error(std::string msg, std::optional<SourceRange> const & loc);

  public:
    std::optional<int> fileid(std::string const & filename) const;

  private:
    std::list<ast::Program> programs;

    template <class TraversalT>
    void traverse_ast(TraversalT & traversal) {
      for (auto const & program: programs) {
        try {
          program.traverse(traversal);
        } catch (CompileError const & e) {
          emit_error(e.message, e.location);
        }
      }
    }

  private:
    SymbolTable tables;

  private:
    std::optional<int> compile__();

  public:
    std::optional<int> compile();
    int backend();

  friend class SymbolScanner;
};

}

#endif // AUTOCOG_COMPILER_STL_DRIVER_HXX
