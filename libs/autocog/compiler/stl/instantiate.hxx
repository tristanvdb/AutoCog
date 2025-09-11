#ifndef AUTOCOG_COMPILER_STL_INSTANTIATE_HXX
#define AUTOCOG_COMPILER_STL_INSTANTIATE_HXX

#include "autocog/compiler/stl/ast.hxx"
#include "autocog/compiler/stl/ir.hxx"
#include "autocog/compiler/stl/diagnostic.hxx"

#include <unordered_map>
#include <optional>
#include <variant>
#include <string>

namespace autocog::compiler::stl {

struct CompileError : std::exception {
  std::string message;
  std::optional<SourceRange> location;
  
  CompileError(std::string msg, std::optional<SourceRange> loc = std::nullopt);
  
  const char * what() const noexcept override;
};

template <class AstNode>
struct AstSymbol {
  ast::Program const & scope;
  AstNode const & node;
  std::string name;

  AstSymbol(
    ast::Program const & scope_,
    AstNode const & node_,
    std::string const & name_
  ) :
    scope(scope_),
    node(node_),
    name(name_)
  {}
};
using RecordSymbol = AstSymbol<ast::Record>;
using PromptSymbol = AstSymbol<ast::Prompt>;

struct PythonSymbol {
  std::string filename;
  std::string callable;
  std::string name;
  
  PythonSymbol(
    std::string const & filename_,
    std::string const & callable_,
    std::string const & name_
  ) :
    filename(filename_),
    callable(callable_),
    name(name_)
  {}
};

struct UnresolvedImport {
  std::string filename;
  std::string objname;
  ast::Import const & import;
  
  UnresolvedImport(
    std::string const & filename_,
    std::string const & objname_,
    ast::Import const & import_
  ) :
    filename(filename_),
    objname(objname_),
    import(import_)
  {}
};

using AnySymbol = std::variant<RecordSymbol, PromptSymbol, PythonSymbol, UnresolvedImport>;
using SymbolTable = std::unordered_map<std::string, AnySymbol>;

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
    std::unordered_map<std::string, ast::Program> const & programs;
    std::list<Diagnostic> & diagnostics;

    std::unordered_map<std::string, ir::VarMap> globals;
    std::unordered_map<std::string, SymbolTable> symbols;

    std::unordered_map<std::string, std::string> exports;
    std::unordered_map<std::string, ir::Prompt> instantiations;

    std::unordered_map<std::string, ir::Record> record_cache;

  private:
    void emit_error(std::string msg, std::optional<SourceRange> const & loc);

  private:
    template <class ScopeT>
    ir::Value evaluate(
      ScopeT const &, ast::Expression  const &, ir::VarMap &
    );

    template <class ScopeT>
    ir::Value evaluate(
      ScopeT const &, ast::Unary const &, ir::VarMap &
    );

    template <class ScopeT>
    ir::Value evaluate(
      ScopeT const &, ast::Binary const &, ir::VarMap &
    );

    template <class ScopeT>
    ir::Value evaluate(
      ScopeT const &, ast::Conditional const &, ir::VarMap &
    );

    template <class ScopeT>
    ir::Value evaluate(
      ScopeT const &, ast::String const &, ir::VarMap &
    );

    template <class ScopeT>
    ir::Value retrieve_value(
      ScopeT const &, std::string const &, ir::VarMap &,
      std::optional<SourceRange> const & = std::nullopt
    );

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
      std::unordered_map<std::string, ast::Program> const & programs_,
      std::list<Diagnostic> & diagnostics_
    );
    void evaluate_defines();
    void generate_symbols();
    void instantiate();
};

}

#include "autocog/compiler/stl/eval-utils.txx"
#include "autocog/compiler/stl/evaluate.txx"

#endif // AUTOCOG_COMPILER_STL_INSTANTIATE_HXX
