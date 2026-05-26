#ifndef AUTOCOG_COMPILER_STL_INSTANTIATION_GRAPH_HXX
#define AUTOCOG_COMPILER_STL_INSTANTIATION_GRAPH_HXX

#include "autocog/compiler/stl/ast.hxx"
#include "autocog/compiler/stl/symbols.hxx"
#include "autocog/compiler/stl/ir.hxx"
#include "autocog/compiler/stl/evaluate.hxx"

#include <deque>
#include <functional>
#include <optional>

namespace autocog::compiler::stl {

class Driver;

using DeclRef = std::variant<
    std::reference_wrapper<const ast::Record>,
    std::reference_wrapper<const ast::Prompt>
>;

struct InstantiationEdge {
    std::string from;
    std::string to;
};

struct InstantiationNode {
    std::string mangled_name;
    std::string base_name;
    int fileid;
    DeclRef ast;
    ir::VarMap context;     // full evaluated context (arguments + defines)
    ir::VarMap arguments;   // just argument values (used for mangling)

    InstantiationNode(
        std::string mangled_name_,
        std::string base_name_,
        int fileid_,
        DeclRef ast_,
        ir::VarMap context_,
        ir::VarMap arguments_
    ) : mangled_name(std::move(mangled_name_)),
        base_name(std::move(base_name_)),
        fileid(fileid_),
        ast(ast_),
        context(std::move(context_)),
        arguments(std::move(arguments_))
    {}
};

struct InstantiationGraph {
    std::unordered_map<std::string, InstantiationNode> nodes;
    std::list<InstantiationEdge> edges;
};

// Resolved symbol after following imports/aliases
struct ResolvedSymbol {
    std::string name;
    int fileid;
    DeclRef decl;
    ir::VarMap partial_kwargs;  // from import/alias config
};

class InstantiationGraphBuilder {
  public:
    InstantiationGraphBuilder(Driver & driver_, Evaluator & evaluator_, InstantiationGraph & graph_);

    void build();

  public:
    // Used by both build() and IR generation (stage 7)
    std::optional<ResolvedSymbol> resolve(std::string const & name, int fileid);
    ir::VarMap eval_context(ResolvedSymbol const & sym, ir::VarMap const & caller_kwargs);
    ir::VarMap extract_arguments(DeclRef const & decl, ir::VarMap const & full_context);
    bool is_python_symbol(std::string const & name, int fileid);

  private:
    ir::VarMap eval_assigns(
        std::list<ast::Assign> const & assigns,
        ir::VarMap & ctx,
        std::string const & scope
    );

    // Process a node: scan its AST for references and enqueue targets
    void process_node(InstantiationNode & node);

    // Handle a discovered reference
    void process_reference(
        std::string const & predecessor,
        std::string const & ref_name,
        ir::VarMap const & kwargs,
        int fileid
    );

    InstantiationGraph & graph;
    Driver & driver;
    Evaluator & evaluator;
    std::deque<std::string> queue;

    static constexpr size_t MAX_INSTANTIATIONS = 10000;
};

}

#endif // AUTOCOG_COMPILER_STL_INSTANTIATION_GRAPH_HXX
