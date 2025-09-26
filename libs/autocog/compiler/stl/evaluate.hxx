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

class Evaluator {
  private:
    std::list<Diagnostic> & diagnostics;

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

  public:
    Evaluator(std::list<Diagnostic> & diagnostics_);

    template <class ScopeT>
    ir::Value retrieve_value(
      ScopeT const &, std::string const &, ir::VarMap &,
      std::optional<SourceRange> const & = std::nullopt
    );
};

}

#include "autocog/compiler/stl/eval-utils.txx"
#include "autocog/compiler/stl/evaluate.txx"

#endif // AUTOCOG_COMPILER_STL_INSTANTIATE_HXX
