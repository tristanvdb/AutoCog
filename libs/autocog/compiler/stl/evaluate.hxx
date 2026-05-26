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
};

}

#include "autocog/compiler/stl/eval-utils.txx"

#endif // AUTOCOG_COMPILER_STL_INSTANTIATE_HXX
