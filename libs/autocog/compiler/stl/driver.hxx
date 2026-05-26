#ifndef AUTOCOG_COMPILER_STL_DRIVER_HXX
#define AUTOCOG_COMPILER_STL_DRIVER_HXX

#include "autocog/compiler/stl/diagnostic.hxx"
#include "autocog/compiler/stl/ast.hxx"
#include "autocog/compiler/stl/symbol-table.hxx"
#include "autocog/compiler/stl/ir.hxx"
#include "autocog/compiler/stl/instantiation-graph.hxx"
#include "autocog/runtime/sta/state.hxx"

#include <list>
#include <memory>
#include <string>
#include <optional>
#include <unordered_map>

namespace autocog::compiler::stl {

class Evaluator;

/**
 * Compilation stage — controls when compile() stops.
 */
enum class CompilationStage {
  Parse       = 1,
  Symbols     = 2,
  Globals     = 3,
  Instantiate = 4,
  Assemble    = 5,
  Generate    = 6,
};

class Driver {
  // ---- Configuration (set before compile) ----
  public:
    std::list<std::string> inputs;
    std::list<std::string> includes;
    ir::VarMap defines;
    std::list<std::string> entry_points = {"main"};

    CompilationStage stage = CompilationStage::Generate;
    std::optional<std::string> output = std::nullopt;
    bool verbose = false;

  // ---- Stage results (populated during compile) ----
  public:
    // Stage 1: Parse
    std::list<ast::Program> programs;
    std::unordered_map<std::string, int> fileids;

    // Stage 2: Symbols
    SymbolTable tables;

    // Stage 3: Globals (evaluator, created here, used by later stages)
    std::unique_ptr<Evaluator> evaluator;

    // Stage 4: Instantiate
    InstantiationGraph graph;
    std::unordered_map<std::string, std::string> entry_point_map;

    // Stage 5: Assemble (IR)
    std::unordered_map<std::string, std::unique_ptr<ir::Record>> records;
    std::unordered_map<std::string, std::unique_ptr<ir::Prompt>> prompts;

    // Stage 6: Generate (STA)
    runtime::sta::Program sta;

  // ---- Diagnostics ----
  public:
    void emit_error(std::string msg, std::optional<SourceRange> const & loc);
    void emit_warning(std::string msg, std::optional<SourceRange> const & loc);
    void emit_note(std::string msg, std::optional<SourceRange> const & loc);
    bool report_errors();

  private:
    unsigned errors = 0;
    unsigned warnings = 0;
    unsigned notes = 0;
    std::list<Diagnostic> diagnostics;

  // ---- Utilities ----
  public:
    std::optional<int> fileid(std::string const & filename) const;
    std::string mangle(std::string const & name, ir::VarMap const & bindings) const;

    template <class TraversalT>
    void traverse_ast(TraversalT & traversal) {
      for (auto const & program : programs) {
        try {
          program.traverse(traversal);
        } catch (CompileError const & e) {
          emit_error(e.message, e.location);
        }
      }
    }

  // ---- Compilation ----
  public:
    std::optional<int> compile();

  private:
    std::optional<int> compile_stages();

    // Individual stages (defined in compile/*.cxx)
    std::optional<int> run_parse();
    std::optional<int> run_symbols();
    std::optional<int> run_globals();
    std::optional<int> run_instantiate();
    std::optional<int> run_assemble();
    std::optional<int> run_generate();
};

}

#endif // AUTOCOG_COMPILER_STL_DRIVER_HXX
