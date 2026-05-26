#ifndef AUTOCOG_COMPILER_STL_INSTANTIATE_HXX
#define AUTOCOG_COMPILER_STL_INSTANTIATE_HXX

#include "autocog/compiler/stl/ast.hxx"
#include "autocog/compiler/stl/ir.hxx"
#include "autocog/compiler/stl/symbol-table.hxx"

#include "autocog/compiler/stl/diagnostic.hxx"

#include <unordered_map>
#include <optional>
#include <variant>
#include <string>

namespace autocog::compiler::stl {

std::string mangle(
  std::string const &,
  ir::VarMap const &
);

using Kwargs = std::unordered_map<std::string, ast::Expression>;

template <class ObjectT>
std::string mangle(
  ObjectT const &,
  ir::VarMap const &
);

class Instantiator {
  private:
    std::list<ast::Program> const & programs;
    std::list<Diagnostic> & diagnostics;
    SymbolTables & tables;

    std::unordered_map<std::string, ir::Prompt> instantiations;
    std::unordered_map<std::string, ir::Record> record_cache;

  private:
    void emit_error(std::string msg, std::optional<SourceRange> const & loc);

  private:    
    void scan_import_statement(
      SymbolTable & symtbl,
      ast::Import const & import
    );

  private:    
    template <class ScopeT>
    ir::VarMap scoped_context(
      ScopeT const &,
      Kwargs const &,
      ir::VarMap const &,
      std::optional<SourceRange> const & = std::nullopt
    );

    template <class ObjectT>
    std::string instantiate(
      ObjectT const &, 
      Kwargs const &,
      ir::VarMap const &,
      std::optional<SourceRange> const & = std::nullopt
    );


  public:
    Instantiator(
      std::list<ast::Program> const & programs_,
      std::list<Diagnostic> & diagnostics_,
      SymbolTables & tables_
    );
    void evaluate_defines();
    void generate_symbols();
    void instantiate();
};

}

#endif // AUTOCOG_COMPILER_STL_INSTANTIATE_HXX
