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

class Instantiator {
  private:
    std::list<Diagnostic> & diagnostics;

    std::unordered_map<std::string, ir::VarMap> globals;

    std::unordered_map<std::string, std::string> declaration_to_file;
    std::unordered_map<std::string, ast::Record const &> records;
    std::unordered_map<std::string, ast::Prompt const &> prompts;

    std::vector<std::pair<std::string, ir::VarMap>> instances;

    std::unordered_map<std::string, ir::Record> record_cache;
    std::unordered_map<std::string, ir::Prompt> instantiations;

  private:
    void emit_error(std::string msg, std::optional<SourceRange> const & loc);

    ir::Value assign   (std::string const &, std::string const &, ast::Expression const &, ir::VarMap &);
    ir::Value evaluate (std::string const &, ast::Expression const &, ir::VarMap &);

    ir::Value evaluateIdentifier    (std::string const & filename, ast::Identifier  const & id,     ir::VarMap & varmap);
    ir::Value evaluateUnaryOp       (std::string const & filename, ast::Unary       const & op,     ir::VarMap & varmap);
    ir::Value evaluateBinaryOp      (std::string const & filename, ast::Binary      const & op,     ir::VarMap & varmap);
    ir::Value evaluateConditionalOp (std::string const & filename, ast::Conditional const & op,     ir::VarMap & varmap);
    ir::Value evaluateParens        (std::string const & filename, ast::Parenthesis const & parens, ir::VarMap & varmap);
    
    std::string formatString(ast::String const & fstring, ir::VarMap & varmap);

  public:
    Instantiator(std::list<Diagnostic> & diagnostics_);

    // Build program's environment
    void defines(ast::Program const & program);

    // Collect records and prompts
    void declarations(ast::Program const & program);

    // Scan for entry then recursively scan for instances
    void entries(ast::Program const & program);

    // Collect all necessary object instantiation (recursive)
    void collect();

    // Perform all instantiations (iterate over prompts then recursively instantiate record using cache)
    void instantiate();
};

}

#endif // AUTOCOG_COMPILER_STL_INSTANTIATE_HXX
