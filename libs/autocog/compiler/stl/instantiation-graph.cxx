
#include "autocog/compiler/stl/instantiation-graph.hxx"
#include "autocog/compiler/stl/driver.hxx"

#include <stdexcept>


namespace autocog::compiler::stl {

// ============================================================================
// InstantiationGraphBuilder
// ============================================================================

InstantiationGraphBuilder::InstantiationGraphBuilder(
    Driver & driver_, Evaluator & evaluator_, InstantiationGraph & graph_
) : graph(graph_), driver(driver_), evaluator(evaluator_), queue() {}

// ----------------------------------------------------------------------------
// resolve: follow imports/aliases to find the actual Record/Prompt declaration
// ----------------------------------------------------------------------------

std::optional<ResolvedSymbol> InstantiationGraphBuilder::resolve(
    std::string const & name, int fileid
) {
    std::string key = std::to_string(fileid) + "::" + name;
    auto it = driver.tables.symbols.find(key);
    if (it == driver.tables.symbols.end()) {
        return std::nullopt;
    }

    auto const & sym = it->second;

    if (auto * rec = std::get_if<RecordSymbol>(&sym)) {
        return ResolvedSymbol{name, fileid, std::cref(rec->node), {}};
    }
    if (auto * pmt = std::get_if<PromptSymbol>(&sym)) {
        return ResolvedSymbol{name, fileid, std::cref(pmt->node), {}};
    }
    if (auto * imp = std::get_if<UnresolvedImport>(&sym)) {
        auto target_name = imp->target.data.name.data.name;
        auto result = resolve(target_name, imp->fileid);
        if (result && !imp->target.data.config.empty()) {
            auto file_scope = std::to_string(fileid);
            auto file_ctx = driver.tables.contexts[file_scope];
            for (auto const & assign : imp->target.data.config) {
                auto arg = assign.data.argument.data.name;
                auto val = evaluator.evaluate_expression(file_scope, assign.data.value, file_ctx);
                result->partial_kwargs[arg] = val;
            }
        }
        return result;
    }
    if (auto * ali = std::get_if<UnresolvedAlias>(&sym)) {
        auto target_name = ali->target.data.name.data.name;
        auto result = resolve(target_name, ali->fileid);
        if (result && !ali->target.data.config.empty()) {
            auto file_scope = std::to_string(ali->fileid);
            auto file_ctx = driver.tables.contexts[file_scope];
            for (auto const & assign : ali->target.data.config) {
                auto arg = assign.data.argument.data.name;
                auto val = evaluator.evaluate_expression(file_scope, assign.data.value, file_ctx);
                result->partial_kwargs[arg] = val;
            }
        }
        return result;
    }

    // DefineSymbol, PythonSymbol — not instantiable
    return std::nullopt;
}

// ----------------------------------------------------------------------------
// eval_context: evaluate a declaration's full context (arguments + defines)
// Caller kwargs override import/alias partial kwargs override defaults.
// Two passes: arguments first, then non-argument defines.
// ----------------------------------------------------------------------------

ir::VarMap InstantiationGraphBuilder::eval_context(
    ResolvedSymbol const & sym,
    ir::VarMap const & caller_kwargs
) {
    // Start from the file-scope global context (file-scope defines and
    // arguments, including values supplied via -D, evaluated during the globals
    // stage). Without this, file-scope arguments supplied only via -D would not
    // be visible at instantiation. Then layer import/alias partial kwargs, then
    // caller overrides.
    ir::VarMap ctx;
    auto file_scope = std::to_string(sym.fileid);
    auto file_ctx_it = driver.tables.contexts.find(file_scope);
    if (file_ctx_it != driver.tables.contexts.end()) {
        ctx = file_ctx_it->second;
    }
    for (auto const & [k, v] : sym.partial_kwargs) {
        ctx[k] = v;
    }
    for (auto const & [k, v] : caller_kwargs) {
        ctx[k] = v;
    }

    auto decl_scope = std::to_string(sym.fileid) + "::" + sym.name;

    // Process constructs in two passes: arguments first, then non-argument defines.
    // We iterate inside the visit to avoid returning different construct list types.
    auto process_defines = [&](auto const & constructs, bool arguments_pass) {
        for (auto const & construct : constructs) {
            std::visit([&](auto const & node) {
                using T = std::decay_t<decltype(node)>;
                if constexpr (std::is_same_v<T, ast::Define>) {
                    // Vocab bindings are not value defines; they are handled by
                    // the vocab translation pass, never evaluated into the value
                    // context.
                    if (node.data.kind == ast::DefineKind::Vocab) return;
                    bool is_arg = (node.data.kind == ast::DefineKind::Argument);
                    if (is_arg != arguments_pass) return;
                    auto name = node.data.name.data.name;
                    if (arguments_pass) {
                        if (ctx.find(name) != ctx.end()) return; // already provided
                        if (node.data.init) {
                            ctx[name] = evaluator.evaluate_expression(
                                decl_scope, node.data.init.value(), ctx
                            );
                        } else {
                            driver.emit_error(
                                "Argument '" + name + "' has no default and was not provided.",
                                node.location
                            );
                        }
                    } else {
                        if (node.data.init) {
                            ctx[name] = evaluator.evaluate_expression(
                                decl_scope, node.data.init.value(), ctx
                            );
                        }
                    }
                }
            }, construct);
        }
    };

    std::visit([&](auto const & d) {
        auto const & constructs = d.get().data.constructs;
        process_defines(constructs, true);   // Pass 1: arguments
        process_defines(constructs, false);  // Pass 2: non-argument defines
    }, sym.decl);

    return ctx;
}

// ----------------------------------------------------------------------------
// extract_arguments: keep only argument values from a full context
// ----------------------------------------------------------------------------

ir::VarMap InstantiationGraphBuilder::extract_arguments(
    DeclRef const & decl, ir::VarMap const & full_context
) {
    ir::VarMap args;
    auto extract = [&](auto const & d) {
        for (auto const & construct : d.get().data.constructs) {
            std::visit([&](auto const & node) {
                using T = std::decay_t<decltype(node)>;
                if constexpr (std::is_same_v<T, ast::Define>) {
                    if (node.data.kind == ast::DefineKind::Argument) {
                        auto name = node.data.name.data.name;
                        auto it = full_context.find(name);
                        if (it != full_context.end()) {
                            args[name] = it->second;
                        }
                    }
                }
            }, construct);
        }
    };
    std::visit(extract, decl);
    return args;
}

// ----------------------------------------------------------------------------
// eval_assigns: evaluate a list of Assign nodes against a context
// ----------------------------------------------------------------------------

ir::VarMap InstantiationGraphBuilder::eval_assigns(
    std::list<ast::Assign> const & assigns,
    ir::VarMap & ctx,
    std::string const & scope
) {
    ir::VarMap kwargs;
    for (auto const & assign : assigns) {
        auto name = assign.data.argument.data.name;
        auto value = evaluator.evaluate_expression(scope, assign.data.value, ctx);
        kwargs[name] = value;
    }
    return kwargs;
}

// ----------------------------------------------------------------------------
// is_python_symbol: check if a name resolves to a PythonSymbol (following imports)
// ----------------------------------------------------------------------------

bool InstantiationGraphBuilder::is_python_symbol(
    std::string const & name, int fileid
) {
    std::string key = std::to_string(fileid) + "::" + name;
    auto it = driver.tables.symbols.find(key);
    if (it == driver.tables.symbols.end()) return false;

    if (std::holds_alternative<PythonSymbol>(it->second)) return true;
    if (auto * imp = std::get_if<UnresolvedImport>(&it->second)) {
        return is_python_symbol(imp->target.data.name.data.name, imp->fileid);
    }
    if (auto * ali = std::get_if<UnresolvedAlias>(&it->second)) {
        return is_python_symbol(ali->target.data.name.data.name, ali->fileid);
    }
    return false;
}

// ----------------------------------------------------------------------------
// process_reference: resolve a name, evaluate context, add node+edge
// ----------------------------------------------------------------------------

void InstantiationGraphBuilder::process_reference(
    std::string const & predecessor,
    std::string const & ref_name,
    ir::VarMap const & kwargs,
    int fileid
) {
    auto resolved = resolve(ref_name, fileid);
    if (!resolved) {
        // Check if the unresolved name is a Python symbol (possibly via imports)
        if (is_python_symbol(ref_name, fileid)) {
            return; // External callable — no instantiation needed
        }
        driver.emit_error("Cannot resolve reference to '" + ref_name + "'.", std::nullopt);
        return;
    }

    auto ctx = eval_context(*resolved, kwargs);
    auto arguments = extract_arguments(resolved->decl, ctx);
    auto mangled = driver.mangle(resolved->name, arguments);

    // Add edge
    graph.edges.push_back({predecessor, mangled});

    // Add node if new
    if (graph.nodes.find(mangled) != graph.nodes.end()) {
        return; // already instantiated
    }

    if (graph.nodes.size() >= MAX_INSTANTIATIONS) {
        driver.emit_error(
            "Exceeded maximum instantiation limit (" + std::to_string(MAX_INSTANTIATIONS) + "). "
            "Possible infinite template recursion.",
            std::nullopt
        );
        return;
    }

    graph.nodes.emplace(
        std::piecewise_construct,
        std::forward_as_tuple(mangled),
        std::forward_as_tuple(mangled, resolved->name, resolved->fileid, resolved->decl, ctx, arguments)
    );
    queue.push_back(mangled);

}

// ----------------------------------------------------------------------------
// process_node: scan a declaration's AST for references
// ----------------------------------------------------------------------------

void InstantiationGraphBuilder::process_node(InstantiationNode & node) {
    auto & ctx = node.context;
    auto scope = std::to_string(node.fileid) + "::" + node.base_name;

    // Helper: process a FormatRef for record references
    auto process_format_ref = [&](ast::FormatRef const & fref) {
        auto const * ident = std::get_if<ast::Identifier>(&fref.data.type);
        if (!ident) return; // built-in format (Text, Enum, Choice)
        auto name = ident->data.name;
        auto kwargs = eval_assigns(fref.data.kwargs, ctx, scope);
        process_reference(node.mangled_name, name, kwargs, node.fileid);
    };

    // Helper: recursively scan a Struct for FormatRef with Identifier
    std::function<void(ast::Struct const &)> process_struct;
    process_struct = [&](ast::Struct const & s) {
        for (auto const & field_ptr : s.data.fields) {
            if (!field_ptr) continue;
            std::visit([&](auto const & type) {
                using T = std::decay_t<decltype(type)>;
                if constexpr (std::is_same_v<T, std::monostate>) {
                    // skip
                } else if constexpr (std::is_same_v<T, ast::FormatRef>) {
                    process_format_ref(type);
                } else if constexpr (std::is_same_v<T, ast::Struct>) {
                    process_struct(type);
                }
            }, field_ptr->data.type);
        }
    };

    std::visit([&](auto const & decl_ref) {
        auto const & decl = decl_ref.get();
        using DeclType = std::decay_t<decltype(decl)>;

        if constexpr (std::is_same_v<DeclType, ast::Record>) {
            // Record body is VARIANT(Struct, FormatRef)
            std::visit([&](auto const & body) {
                using T = std::decay_t<decltype(body)>;
                if constexpr (std::is_same_v<T, ast::FormatRef>) {
                    process_format_ref(body);
                } else if constexpr (std::is_same_v<T, ast::Struct>) {
                    process_struct(body);
                }
            }, decl.data.record);

        } else { // Prompt
            // Fields
            if (decl.data.fields) {
                process_struct(decl.data.fields.value());
            }

            // Constructs: scan for Flow and Channel
            for (auto const & construct : decl.data.constructs) {
                std::visit([&](auto const & cnode) {
                    using T = std::decay_t<decltype(cnode)>;
                    if constexpr (std::is_same_v<T, ast::Flow>) {
                        for (auto const & edge : cnode.data.edges) {
                            auto const & objref = edge.data.prompt;
                            auto name = objref.data.name.data.name;
                            auto kwargs = eval_assigns(objref.data.config, ctx, scope);
                            process_reference(node.mangled_name, name, kwargs, node.fileid);
                        }
                    } else if constexpr (std::is_same_v<T, ast::Channel>) {
                        for (auto const & link : cnode.data.links) {
                            auto const * call = std::get_if<ast::Call>(&link.data.source);
                            if (call) {
                                auto const & objref = call->data.entry;
                                auto name = objref.data.name.data.name;
                                auto kwargs = eval_assigns(objref.data.config, ctx, scope);
                                process_reference(node.mangled_name, name, kwargs, node.fileid);
                            }
                        }
                    }
                }, construct);
            }
        }
    }, node.ast);
}

// ----------------------------------------------------------------------------
// build: main entry — seed from entry points, process BFS queue
// ----------------------------------------------------------------------------

void InstantiationGraphBuilder::build() {

    // Seed from entry points
    for (auto const & entry_name : driver.entry_points) {
        // Try to find in any parsed file (check each file's scope)
        std::optional<ResolvedSymbol> resolved;
        for (auto const & [filename, fid] : driver.fileids) {
            resolved = resolve(entry_name, fid);
            if (resolved) break;
        }
        // Also try file 0 directly for the common case
        if (!resolved) {
            resolved = resolve(entry_name, 0);
        }

        if (!resolved) {
            driver.emit_error("Entry point '" + entry_name + "' not found.", std::nullopt);
            continue;
        }

        auto ctx = eval_context(*resolved, {});
        auto arguments = extract_arguments(resolved->decl, ctx);
        auto mangled = driver.mangle(resolved->name, arguments);

        driver.entry_point_map[entry_name] = mangled;

        if (graph.nodes.find(mangled) != graph.nodes.end()) continue;

        graph.nodes.emplace(
            std::piecewise_construct,
            std::forward_as_tuple(mangled),
            std::forward_as_tuple(mangled, resolved->name, resolved->fileid, resolved->decl, ctx, arguments)
        );
        queue.push_back(mangled);

    }

    // BFS
    while (!queue.empty()) {
        auto mangled = queue.front();
        queue.pop_front();

        auto it = graph.nodes.find(mangled);
        if (it == graph.nodes.end()) continue;

        process_node(it->second);
    }

}

} // namespace autocog::compiler::stl
