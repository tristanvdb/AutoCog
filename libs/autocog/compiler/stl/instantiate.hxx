#ifndef AUTOCOG_COMPILER_STL_INSTANTIATE_HXX
#define AUTOCOG_COMPILER_STL_INSTANTIATE_HXX

#include "autocog/compiler/stl/ast.hxx"

#include <unordered_map>
#include <variant>
#include <string>

namespace autocog::compiler::stl {

namespace ir {
  using Value = std::variant<int, float, bool, std::string>;
  struct Record {};
  struct Prompt {};
}

using VarMap = std::unordered_map<std::string, ir::Value>;

struct Instantiation {
  std::list<Diagnostic> & diagnostics;

  Instantiation(std::list<Diagnostic> & diagnostics_);

  void emit_error(std::string msg);
};

class Instantiator {
  private:
    std::list<Diagnostic> & diagnostics;

    std::unordered_map<std::string, VarMap> programs;

    std::unordered_map<std::string, std::string> declaration_to_file;
    std::unordered_map<std::string, ast::Record const &> records;
    std::unordered_map<std::string, ast::Prompt const &> prompts;

    std::vector<std::pair<std::string, VarMap>> instances;

    std::unordered_map<std::string, ir::Record> record_cache;
    std::unordered_map<std::string, ir::Prompt> instantiations;

  private:

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
