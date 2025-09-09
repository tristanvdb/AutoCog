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

    ir::Value assign   (ast::Program const &, std::string const &, ast::Expression const &, ir::VarMap &);
    ir::Value evaluate (ast::Program const &, ast::Expression const &, ir::VarMap &);

    ir::Value evaluateIdentifier    (ast::Program const &, ast::Identifier  const & id,     ir::VarMap & varmap);
    ir::Value evaluateUnaryOp       (ast::Program const &, ast::Unary       const & op,     ir::VarMap & varmap);
    ir::Value evaluateBinaryOp      (ast::Program const &, ast::Binary      const & op,     ir::VarMap & varmap);
    ir::Value evaluateConditionalOp (ast::Program const &, ast::Conditional const & op,     ir::VarMap & varmap);
    ir::Value evaluateParens        (ast::Program const &, ast::Parenthesis const & parens, ir::VarMap & varmap);
    
    std::string formatString(ast::Program const &, ast::String const & fstring, ir::VarMap & varmap);

    void define_one(ast::Program const &, ast::Define const & defn, ir::VarMap & varmap);
    bool define_one(ast::Program const &, std::string const & varname, ir::VarMap & varmap);

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
