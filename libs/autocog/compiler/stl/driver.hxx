#ifndef AUTOCOG_COMPILER_STL_DRIVER_HXX
#define AUTOCOG_COMPILER_STL_DRIVER_HXX

#include "autocog/utilities/errors.hxx"
#include "autocog/utilities/location.hxx"
#include "autocog/data/diagnostic.hxx"
#include "autocog/compiler/stl/ast.hxx"
#include "autocog/compiler/stl/symbol-table.hxx"
#include "autocog/compiler/stl/ir.hxx"
#include "autocog/compiler/stl/instantiation-graph.hxx"
#include "autocog/data/sta.hxx"

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

    // How far compile() runs. Set from the deepest requested emit output below.
    CompilationStage stage = CompilationStage::Generate;

    // Per-format emit outputs (stlc --ast/--graph/--ir/--sta). Each, if set,
    // names the file to write that artifact to (/dev/stdout for stdout).
    // Several may be set to emit multiple artifacts in one run; compile() runs
    // up to the deepest requested stage and main serializes each.
    std::optional<std::string> out_ast   = std::nullopt;
    std::optional<std::string> out_graph = std::nullopt;
    std::optional<std::string> out_ir    = std::nullopt;
    std::optional<std::string> out_sta   = std::nullopt;

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
    autocog::data::STA sta;

  // ---- Diagnostics ----
  public:
    void emit_error(std::string msg, std::optional<autocog::location::SourceRange> const & loc);
    void emit_warning(std::string msg, std::optional<autocog::location::SourceRange> const & loc);
    void emit_note(std::string msg, std::optional<autocog::location::SourceRange> const & loc);
    bool report_errors();

    // When true (default, used by the CLI tools), report_errors() prints each
    // diagnostic to stderr. The Python binding sets this false: it retrieves
    // the retained diagnostics and routes them through Python's logging instead,
    // so they are not also printed here (which would double-log).
    bool print_diagnostics = true;

    // All diagnostics seen across every stage, retained for retrieval after
    // compile() returns (report_errors() moves into here rather than discarding).
    std::list<autocog::data::Diagnostic> const & diagnostics_log() const { return collected; }

  private:
    unsigned errors = 0;
    unsigned warnings = 0;
    unsigned notes = 0;
    std::list<autocog::data::Diagnostic> diagnostics;
    std::list<autocog::data::Diagnostic> collected;

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
