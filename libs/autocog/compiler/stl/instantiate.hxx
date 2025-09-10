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
  ir::VarMap arguments;

  AstSymbol(
    ast::Program const & scope_,
    AstNode const & node_,
    std::string const & name_
  ) :
    scope(scope_),
    node(node_),
    name(name_),
    arguments()
  {}
};

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

class Instantiator {
  public:
    using RecordSymbol = AstSymbol<ast::Record>;
    using PromptSymbol = AstSymbol<ast::Prompt>;
    using UnresolvedImport = AstSymbol<ast::Import>;
    using AnySymbol = std::variant<RecordSymbol, PromptSymbol, PythonSymbol, UnresolvedImport>;
    using SymbolTable = std::unordered_map<std::string, AnySymbol>;

  private:
    std::unordered_map<std::string, ast::Program> const & programs;
    std::list<Diagnostic> & diagnostics;

    std::unordered_map<std::string, ir::VarMap> globals;
    std::unordered_map<std::string, SymbolTable> symbols;

//  std::queue<std::pair<std::string, ir::VarMap>> instantiation_queue;

//  std::unordered_map<std::string, ir::Record> record_cache;
//  std::unordered_map<std::string, ir::Prompt> instantiations;

  private:
    void emit_error(std::string msg, std::optional<SourceRange> const & loc);

  private:
    ir::Value evaluate (ast::Program const &, ast::Expression const &, ir::VarMap &);
    ir::Value evaluateUnaryOp       (ast::Program const &, ast::Unary       const & op,     ir::VarMap & varmap);
    ir::Value evaluateBinaryOp      (ast::Program const &, ast::Binary      const & op,     ir::VarMap & varmap);
    ir::Value evaluateConditionalOp (ast::Program const &, ast::Conditional const & op,     ir::VarMap & varmap);
    std::string formatString(ast::Program const &, ast::String const & fstring, ir::VarMap & varmap);
    ir::Value retrieve_value(
      ast::Program const & program,
      std::string const & varname,
      ir::VarMap & varmap,
      std::optional<SourceRange> const & loc = std::nullopt
    );

  private:    
    void scan_import_statement(
      ast::Program const & program,
      ast::Import const & import
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

#endif // AUTOCOG_COMPILER_STL_INSTANTIATE_HXX
