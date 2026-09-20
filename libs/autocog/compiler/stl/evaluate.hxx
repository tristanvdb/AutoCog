#ifndef AUTOCOG_COMPILER_STL_INSTANTIATE_HXX
#define AUTOCOG_COMPILER_STL_INSTANTIATE_HXX

#include "autocog/utilities/exception.hxx"
#include "autocog/utilities/location.hxx"
#include "autocog/compiler/stl/ast.hxx"
#include "autocog/compiler/stl/ir.hxx"
#include "autocog/data/diagnostic.hxx"

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
    std::list<autocog::data::Diagnostic> & diagnostics;
    SymbolTable & tables;

  private:
    void emit_error(std::string msg, std::optional<autocog::location::SourceRange> const & loc);

  private:
    // Residualizing evaluation: folds what is known and keeps what is not. The
    // result is a literal for every expression that names only symbols of the
    // program, which is nearly all of them.
    ir::Expr evaluate(
      std::string const &, ast::Expression  const &, ir::VarMap &
    );

    ir::Expr evaluate(
      std::string const &, ast::Unary const &, ir::VarMap &
    );

    ir::Expr evaluate(
      std::string const &, ast::Binary const &, ir::VarMap &
    );

    ir::Expr evaluate(
      std::string const &, ast::Conditional const &, ir::VarMap &
    );

    ir::Expr evaluate(
      std::string const &, ast::String const &, ir::VarMap &
    );

  public:
    Evaluator(std::list<autocog::data::Diagnostic> & diagnostics_, SymbolTable & tables);

    // What a name is bound to, as an expression.
    ir::Expr retrieve(
      std::string const &, std::string const &, ir::VarMap &,
      std::optional<autocog::location::SourceRange> const & = std::nullopt
    );

    // ... and the same, for a position that requires a constant.
    ir::Value retrieve_value(
      std::string const &, std::string const &, ir::VarMap &,
      std::optional<autocog::location::SourceRange> const & = std::nullopt
    );

    // Evaluate an expression where a constant is required: the common case,
    // and the one that reports which reference made it impossible.
    ir::Value evaluate_expression(
      std::string const & scope, ast::Expression const & expr, ir::VarMap & varmap
    );

    // Evaluate an expression in a position that can wait: everything knowable
    // now is folded, and what is left must be bindable by `latest` at the
    // latest.
    ir::Expr residualize(
      std::string const & scope, ast::Expression const & expr, ir::VarMap & varmap,
      autocog::data::Stage latest
    );

    // The value of a folded expression, or a diagnostic naming what deferred.
    static ir::Value require_constant(
      ir::Expr const &, std::optional<autocog::location::SourceRange> const & = std::nullopt
    );

    // Vocab translation: turn a vocab expression (AST) into a resolved,
    // reference-free ir::VocabExpr. Identifier leaves are resolved (scope-walk,
    // must be vocab symbols) and inlined; f-strings in tokenize/regex are
    // evaluated in `ctx`; non-vocab nodes are a type error. resolve_vocab
    // memoizes by mangled name (so context-dependent f-strings are kept
    // distinct, matching graph instantiation identity) and detects cycles.
    std::unique_ptr<ir::VocabExpr> translate_vocab(
      std::string const & scope, ast::Expression const & expr, ir::VarMap & ctx,
      std::optional<autocog::location::SourceRange> const & loc = std::nullopt
    );

  private:
    std::unique_ptr<ir::VocabExpr> resolve_vocab(
      std::string const & scope, std::string const & name, ir::VarMap & ctx,
      std::optional<autocog::location::SourceRange> const & loc
    );

    std::map<std::string, std::unique_ptr<ir::VocabExpr>> vocab_cache;
    std::set<std::string> vocab_in_progress;
};

}

#endif // AUTOCOG_COMPILER_STL_INSTANTIATE_HXX
