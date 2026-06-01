#ifndef AUTOCOG_COMPILER_STL_INSTANTIATE_HXX
#define AUTOCOG_COMPILER_STL_INSTANTIATE_HXX

#include "autocog/compiler/stl/ast.hxx"
#include "autocog/compiler/stl/ir.hxx"
#include "autocog/compiler/stl/diagnostic.hxx"

#include <unordered_map>
#include <optional>
#include <variant>
#include <string>
#include <map>
#include <set>
#include <memory>

namespace autocog::compiler::stl {

class SymbolTable;
class Evaluator {
  private:
    std::list<Diagnostic> & diagnostics;
    SymbolTable & tables;

  private:
    void emit_error(std::string msg, std::optional<SourceRange> const & loc);

  private:
    ir::Value evaluate(
      std::string const &, ast::Expression  const &, ir::VarMap &
    );

    ir::Value evaluate(
      std::string const &, ast::Unary const &, ir::VarMap &
    );

    ir::Value evaluate(
      std::string const &, ast::Binary const &, ir::VarMap &
    );

    ir::Value evaluate(
      std::string const &, ast::Conditional const &, ir::VarMap &
    );

    ir::Value evaluate(
      std::string const &, ast::String const &, ir::VarMap &
    );

  public:
    Evaluator(std::list<Diagnostic> & diagnostics_, SymbolTable & tables);

    ir::Value retrieve_value(
      std::string const &, std::string const &, ir::VarMap &,
      std::optional<SourceRange> const & = std::nullopt
    );

    // Public interface for evaluating expressions
    ir::Value evaluate_expression(
      std::string const & scope, ast::Expression const & expr, ir::VarMap & varmap
    );

    // Vocab translation: turn a vocab expression (AST) into a resolved,
    // reference-free ir::VocabExpr. Identifier leaves are resolved (scope-walk,
    // must be vocab symbols) and inlined; f-strings in tokenize/regex are
    // evaluated in `ctx`; non-vocab nodes are a type error. resolve_vocab
    // memoizes by mangled name (so context-dependent f-strings are kept
    // distinct, matching graph instantiation identity) and detects cycles.
    std::unique_ptr<ir::VocabExpr> translate_vocab(
      std::string const & scope, ast::Expression const & expr, ir::VarMap & ctx,
      std::optional<SourceRange> const & loc = std::nullopt
    );

  private:
    std::unique_ptr<ir::VocabExpr> resolve_vocab(
      std::string const & scope, std::string const & name, ir::VarMap & ctx,
      std::optional<SourceRange> const & loc
    );

    std::map<std::string, std::unique_ptr<ir::VocabExpr>> vocab_cache;
    std::set<std::string> vocab_in_progress;
};

}

#include "autocog/compiler/stl/eval-utils.txx"

#endif // AUTOCOG_COMPILER_STL_INSTANTIATE_HXX
