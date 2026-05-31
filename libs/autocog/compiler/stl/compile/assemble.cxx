
#include "autocog/compiler/stl/driver.hxx"
#include "autocog/compiler/stl/evaluate.hxx"
#include "autocog/compiler/stl/instantiation-graph.hxx"
#include "autocog/logging.hxx"

namespace autocog::compiler::stl {

// ============================================================================
// IR assembly helpers (anonymous namespace)
// ============================================================================

namespace {

static std::optional<int> eval_opt_int(
    std::optional<ast::Expression> const & expr,
    Evaluator & evaluator,
    std::string const & scope,
    ir::VarMap & ctx
) {
    if (!expr) return std::nullopt;
    auto val = evaluator.evaluate_expression(scope, expr.value(), ctx);
    if (auto * iv = std::get_if<int>(&val)) return *iv;
    return std::nullopt;
}

// Structural scope of a `search { }` block, for category-placement rules.
enum class SearchScope { File, Prompt, Record };

// Lower an `ast::Search` block into ir::SearchPolicies. Each param's locator is a
// dotted path: the FIRST segment is the category (text/enum/branch/flow/queue,
// or "" when no prefix), the remaining segments are joined with '.' as the
// param key. The RHS expression is evaluated at compile time (constexpr-style)
// in the current scope/context. The map is OPEN — unknown params are kept
// verbatim and resolved/validated downstream. Category PLACEMENT is checked
// here (graph->IR traversal), not at parse time: `flow` and `queue` configure
// prompt-global behavior and are rejected at record scope.
static void lower_search(
    ast::Search const & search,
    Evaluator & evaluator,
    std::string const & scope,
    ir::VarMap & ctx,
    SearchScope structural_scope,
    Driver & driver,
    ir::SearchPolicies & out
) {
    for (auto const & param : search.data.params) {
        auto const & loc = param.data.locator;
        if (loc.empty()) continue;
        std::string category;
        std::string key;
        if (loc.size() == 1) {
            // No category prefix: a search param must be category-qualified
            // (text/enum/branch/flow/queue). An un-prefixed param is almost
            // certainly an authoring mistake — warn and drop it (never stored).
            driver.emit_warning(
                "search parameter '" + loc.front().data.name + "' has no category "
                "prefix (expected e.g. text." + loc.front().data.name + "); ignoring",
                param.location);
            continue;
        } else {
            auto it = loc.begin();
            category = it->data.name;
            ++it;
            for (bool first = true; it != loc.end(); ++it) {
                if (!first) key += ".";
                key += it->data.name;
                first = false;
            }
        }
        // Scope rule: flow/queue are global (file) + prompt only.
        if ((category == "flow" || category == "queue") &&
            structural_scope == SearchScope::Record) {
            driver.emit_error(
                "search category '" + category + "' is only valid at file or "
                "prompt scope, not inside a record",
                param.location);
            continue;
        }
        auto val = evaluator.evaluate_expression(scope, param.data.value, ctx);
        out[category][key] = val;
    }
}

// Merge search policy layers, innermost-wins, per-param. `base` is the outer
// (enclosing) policy; `over` overrides it. Categories and params present only
// in `base` are inherited; those in `over` replace the corresponding entry.
static ir::SearchPolicies merge_search(
    ir::SearchPolicies base, ir::SearchPolicies const & over
) {
    for (auto const & [category, params] : over) {
        for (auto const & [key, val] : params) {
            base[category][key] = val;
        }
    }
    return base;
}

// Keep only the listed categories (drop the rest). Used to prune a merged
// policy to what a given site consumes (leaf: text/enum + branch; container:
// branch only; prompt: flow/queue).
static ir::SearchPolicies prune_search(
    ir::SearchPolicies const & in,
    std::initializer_list<char const *> keep
) {
    ir::SearchPolicies out;
    for (char const * cat : keep) {
        auto it = in.find(cat);
        if (it != in.end() && !it->second.empty()) out[cat] = it->second;
    }
    return out;
}

// Collect an inline struct's own `search { }` constructs into a SearchPolicies
// (merged in document order). Constructs live on ast::Struct.constructs as a
// variant of Annotate|Search; only Search contributes here.
static ir::SearchPolicies collect_struct_search(
    ast::Struct const & s,
    Evaluator & evaluator,
    std::string const & scope,
    ir::VarMap & ctx,
    Driver & driver
) {
    ir::SearchPolicies out;
    for (auto const & construct : s.data.constructs) {
        std::visit([&](auto const & c) {
            using T = std::decay_t<decltype(c)>;
            if constexpr (std::is_same_v<T, ast::Search>) {
                lower_search(c, evaluator, scope, ctx, SearchScope::Record, driver, out);
            }
        }, construct);
    }
    return out;
}

static std::vector<ir::PathStep> convert_path(
    ast::Path const & path,
    Evaluator & evaluator,
    std::string const & scope,
    ir::VarMap & ctx
) {
    std::vector<ir::PathStep> steps;
    for (auto const & step : path.data.steps) {
        ir::PathStep ps;
        ps.name = step.data.field.data.name;
        auto lower = eval_opt_int(step.data.lower, evaluator, scope, ctx);
        auto upper = eval_opt_int(step.data.upper, evaluator, scope, ctx);
        // Mirror the grammar faithfully (Python slice semantics):
        //   no bracket            -> nullopt selector (whole field, may be scalar)
        //   [i]   (!is_range)     -> index i (scalar element)
        //   [lo:hi] (is_range)    -> slice with optional open bounds (list)
        if (step.data.is_range) {
            ps.selector = ir::StepRange{lower, upper};
        } else if (lower) {
            ps.selector = *lower;          // index -> scalar
        }   // else: bare field, selector stays nullopt
        steps.push_back(std::move(ps));
    }
    return steps;
}

template <typename ClauseList>
static std::vector<ir::Clause> convert_clauses_from_link(
    ClauseList const & clauses,
    Evaluator & evaluator,
    std::string const & scope,
    ir::VarMap & ctx
) {
    std::vector<ir::Clause> result;
    for (auto const & clause : clauses) {
        std::visit([&](auto const & c) {
            using T = std::decay_t<decltype(c)>;
            if constexpr (std::is_same_v<T, ast::Bind>) {
                ir::BindClause bc;
                bc.source = convert_path(c.data.source, evaluator, scope, ctx);
                if (c.data.target) {
                    bc.target = convert_path(*c.data.target, evaluator, scope, ctx);
                }
                result.push_back(std::move(bc));
            } else if constexpr (std::is_same_v<T, ast::Ravel>) {
                ir::RavelClause rc;
                rc.depth = eval_opt_int(c.data.depth, evaluator, scope, ctx);
                if (c.data.target) {
                    rc.target = convert_path(*c.data.target, evaluator, scope, ctx);
                }
                result.push_back(std::move(rc));
            } else if constexpr (std::is_same_v<T, ast::Wrap>) {
                ir::WrapClause wc;
                if (c.data.target) {
                    wc.target = convert_path(*c.data.target, evaluator, scope, ctx);
                }
                result.push_back(std::move(wc));
            } else if constexpr (std::is_same_v<T, ast::Prune>) {
                ir::PruneClause pc;
                pc.target = convert_path(c.data.target, evaluator, scope, ctx);
                result.push_back(std::move(pc));
            } else if constexpr (std::is_same_v<T, ast::Mapped>) {
                ir::MappedClause mc;
                if (c.data.target) {
                    mc.target = convert_path(*c.data.target, evaluator, scope, ctx);
                }
                result.push_back(std::move(mc));
            }
        }, clause);
    }
    return result;
}

static std::optional<std::string> eval_alias(
    ast::Expression const & expr,
    Evaluator & evaluator,
    std::string const & scope,
    ir::VarMap & ctx
) {
    auto const * ident = std::get_if<ast::Identifier>(&expr.data.expr);
    if (ident) {
        return ident->data.name;
    }
    auto val = evaluator.evaluate_expression(scope, expr, ctx);
    if (auto * sv = std::get_if<std::string>(&val)) return *sv;
    return std::nullopt;
}

// Forward declaration
// Result of lowering a field's type. Carries the structural format variant,
// optional named-record provenance, and (for record refs) the record's own
// search { } policy to fold into the cascade.
struct FormatResult {
    ir::Format format;
    std::optional<std::string> refname;
    ir::VarMap refargs;
    ir::SearchPolicies own_search;   // the referenced record's own search { }
    std::vector<std::string> desc;   // annotations from a referenced record
};

static FormatResult generate_format(
    std::string const & field_name,
    ast::FormatRef const & fref,
    Evaluator & evaluator,
    std::string const & scope,
    ir::VarMap & ctx,
    int fileid,
    InstantiationGraphBuilder & builder,
    Driver & driver,
    std::map<std::string, ir::VocabExpr> & prompt_vocabs
);

static void generate_fields(
    ast::Struct const & s,
    Evaluator & evaluator,
    std::string const & scope,
    ir::VarMap & ctx,
    int fileid,
    InstantiationGraphBuilder & builder,
    Driver & driver,
    int depth,
    std::vector<std::unique_ptr<ir::Field>> & out,
    ir::SearchPolicies const & enclosing,
    std::map<std::string, ir::VocabExpr> & prompt_vocabs
) {
    // The policy in scope for this struct's fields = enclosing (file/prompt/
    // outer structs) merged with this struct's own search { } (innermost wins).
    ir::SearchPolicies here = merge_search(enclosing,
        collect_struct_search(s, evaluator, scope, ctx, driver));

    int index = 0;
    for (auto const & field_ptr : s.data.fields) {
        if (!field_ptr) continue;
        auto const & field = *field_ptr;
        auto name = field.data.name.data.name;
        auto f = std::make_unique<ir::Field>(name, depth, index++);

        auto lower = eval_opt_int(field.data.lower, evaluator, scope, ctx);
        auto upper = eval_opt_int(field.data.upper, evaluator, scope, ctx);
        if (lower && upper) {
            f->range = std::make_pair(*lower, *upper);
        } else if (lower) {
            f->range = std::make_pair(*lower, *lower);
        }

        std::visit([&](auto const & type) {
            using T = std::decay_t<decltype(type)>;
            if constexpr (std::is_same_v<T, ast::FormatRef>) {
                auto res = generate_format(name, type, evaluator, scope, ctx, fileid, builder, driver, prompt_vocabs);
                f->format = std::move(res.format);
                f->refname = res.refname;
                f->refargs = res.refargs;
                for (auto & d : res.desc) f->desc.push_back(d);
                // The field's policy in scope, plus the referenced record's own
                // search { } as the innermost layer (only meaningful for a
                // collapsed self-form record; empty otherwise).
                ir::SearchPolicies field_here = merge_search(here, res.own_search);
                if (f->is_struct()) {
                    f->search = prune_search(field_here, {"branch"});
                } else {
                    f->search = prune_search(field_here, {"text", "enum", "branch"});
                }
            } else if constexpr (std::is_same_v<T, ast::Struct>) {
                // The inline struct's own search { } is the innermost policy
                // layer for this field (and for its sub-fields if a container).
                ir::SearchPolicies struct_here = merge_search(here,
                    collect_struct_search(type, evaluator, scope, ctx, driver));
                // Inline-struct annotations -> field desc.
                for (auto const & construct : type.data.constructs) {
                    std::visit([&](auto const & c) {
                        using C = std::decay_t<decltype(c)>;
                        if constexpr (std::is_same_v<C, ast::Annotate>) {
                            for (auto const & ann : c.data.annotations) {
                                if (!ann.data.path) {
                                    auto val = evaluator.evaluate_expression(scope, ann.data.description, ctx);
                                    if (auto * sv = std::get_if<std::string>(&val)) f->desc.push_back(*sv);
                                }
                            }
                        }
                    }, construct);
                }
                if (type.data.selfform) {
                    // Self-form inline struct: a leaf. Collapse the format in.
                    auto res = generate_format(name, type.data.selfform.value(), evaluator, scope, ctx, fileid, builder, driver, prompt_vocabs);
                    f->format = std::move(res.format);
                    f->refname = res.refname;
                    f->refargs = res.refargs;
                    for (auto & d : res.desc) f->desc.push_back(d);
                    ir::SearchPolicies field_here = merge_search(struct_here, res.own_search);
                    if (f->is_struct()) {
                        f->search = prune_search(field_here, {"branch"});
                    } else {
                        f->search = prune_search(field_here, {"text", "enum", "branch"});
                    }
                } else {
                    // Field-list inline struct: a container. The recursive
                    // generate_fields merges this struct's own search itself, so
                    // pass `here` (not struct_here) to avoid double-counting.
                    std::vector<std::unique_ptr<ir::Field>> sub;
                    generate_fields(type, evaluator, scope, ctx, fileid, builder, driver, depth + 1, sub, here, prompt_vocabs);
                    f->format = std::move(sub);
                    f->search = prune_search(struct_here, {"branch"});
                }
            }
        }, field.data.type);

        out.push_back(std::move(f));
    }
}

static FormatResult generate_format(
    std::string const & field_name,
    ast::FormatRef const & fref,
    Evaluator & evaluator,
    std::string const & scope,
    ir::VarMap & ctx,
    int fileid,
    InstantiationGraphBuilder & builder,
    Driver & driver,
    std::map<std::string, ir::VocabExpr> & prompt_vocabs
) {
    FormatResult result;
    std::visit([&](auto const & type) {
        using T = std::decay_t<decltype(type)>;

        if constexpr (std::is_same_v<T, ast::Text>) {
            ir::Completion fmt;
            for (auto const & assign : fref.data.kwargs) {
                auto k = assign.data.argument.data.name;
                if (k == "vocab") {
                    // The vocab kwarg's value is a vocab expression. Translate it
                    // to a resolved, reference-free VocabExpr, register it in the
                    // prompt's vocab table keyed by its hash, and make the field
                    // reference that entry.
                    auto ve = evaluator.translate_vocab(scope, assign.data.value, ctx, assign.location);
                    if (ve) {
                        std::string key = "vocab_" + ir::vocab_hash(*ve);
                        fmt.vocab = key;
                        prompt_vocabs.emplace(key, std::move(*ve));
                    }
                    continue;
                }
                auto v = evaluator.evaluate_expression(scope, assign.data.value, ctx);
                if (k == "length" && std::holds_alternative<int>(v)) {
                    fmt.length = std::get<int>(v);
                } else {
                    driver.emit_error(
                        "'" + k + "' is not a structural parameter of text "
                        "(only length, vocab). Search tuning belongs in a "
                        "search { text." + k + " is ...; } block.",
                        assign.location);
                }
            }
            result.format = std::move(fmt);

        } else if constexpr (std::is_same_v<T, ast::Enum>) {
            ir::Enum fmt;
            for (auto const & s : type.data.enumerators) {
                fmt.values.push_back(s.data.value);
            }
            result.format = std::move(fmt);

        } else if constexpr (std::is_same_v<T, ast::Choice>) {
            ir::Choice fmt;
            fmt.mode = (type.data.mode == ast::ChoiceKind::Select) ? "select" : "repeat";
            auto steps = convert_path(type.data.source, evaluator, scope, ctx);
            fmt.path = std::move(steps);
            for (auto const & assign : fref.data.kwargs) {
                auto k = assign.data.argument.data.name;
                driver.emit_error(
                    "'" + k + "' is not a structural parameter of "
                    + fmt.mode + " (only path). Search tuning belongs in a "
                    "search { } block.",
                    assign.location);
            }
            result.format = std::move(fmt);

        } else if constexpr (std::is_same_v<T, ast::Identifier>) {
            auto rec_name = type.data.name;
            ir::VarMap kwargs;
            for (auto const & assign : fref.data.kwargs) {
                auto k = assign.data.argument.data.name;
                auto v = evaluator.evaluate_expression(scope, assign.data.value, ctx);
                kwargs[k] = v;
            }
            auto resolved = builder.resolve(rec_name, fileid);
            if (!resolved) { result.format = std::vector<std::unique_ptr<ir::Field>>{}; return; }

            auto target_ctx = builder.eval_context(*resolved, kwargs);
            auto target_scope = std::to_string(resolved->fileid) + "::" + resolved->name;

            auto const * rec_ref = std::get_if<std::reference_wrapper<const ast::Record>>(&resolved->decl);
            if (!rec_ref) { result.format = std::vector<std::unique_ptr<ir::Field>>{}; return; }

            auto const & rec_decl = rec_ref->get();
            result.refname = rec_name;
            result.refargs = kwargs;

            // The referenced record's own search { } (folds into the field's
            // policy as an inner layer).
            for (auto const & construct : rec_decl.data.constructs) {
                std::visit([&](auto const & c) {
                    using C = std::decay_t<decltype(c)>;
                    if constexpr (std::is_same_v<C, ast::Search>) {
                        lower_search(c, evaluator, target_scope, target_ctx,
                                     SearchScope::Record, driver, result.own_search);
                    } else if constexpr (std::is_same_v<C, ast::Annotate>) {
                        for (auto const & ann : c.data.annotations) {
                            if (!ann.data.path) {
                                auto val = evaluator.evaluate_expression(target_scope, ann.data.description, target_ctx);
                                if (auto * sv = std::get_if<std::string>(&val)) {
                                    result.desc.push_back(*sv);
                                }
                            }
                        }
                    }
                }, construct);
            }

            std::visit([&](auto const & body) {
                using B = std::decay_t<decltype(body)>;
                if constexpr (std::is_same_v<B, ast::Struct>) {
                    // Multi-field record → struct (sub-fields). The record's own
                    // search { } also flows down as enclosing for its fields.
                    std::vector<std::unique_ptr<ir::Field>> sub;
                    generate_fields(body, evaluator, target_scope, target_ctx, resolved->fileid, builder, driver, 1, sub, result.own_search, prompt_vocabs);
                    result.format = std::move(sub);
                } else if constexpr (std::is_same_v<B, ast::FormatRef>) {
                    // Self-form record → COLLAPSE into this field: take the
                    // record's format directly. refname/own_search already set.
                    auto inner = generate_format(field_name, body, evaluator, target_scope, target_ctx, resolved->fileid, builder, driver, prompt_vocabs);
                    result.format = std::move(inner.format);
                    // A self-form record could itself reference another record;
                    // merge nested provenance/policy conservatively.
                    if (!inner.own_search.empty())
                        result.own_search = merge_search(result.own_search, inner.own_search);
                    for (auto & d : inner.desc) result.desc.push_back(d);
                }
            }, rec_decl.data.record);
        }
    }, fref.data.type);
    return result;
}

// ============================================================================
// Assemble records from graph nodes
// ============================================================================

static void assemble_records(
    InstantiationGraph & graph,
    InstantiationGraphBuilder & builder,
    Evaluator & evaluator,
    Driver & driver
) {
    for (auto & [mangled, node] : graph.nodes) {
        auto const * rec_ref = std::get_if<std::reference_wrapper<const ast::Record>>(&node.ast);
        if (!rec_ref) continue;

        auto const & decl = rec_ref->get();
        auto scope = std::to_string(node.fileid) + "::" + node.base_name;
        auto rec = std::make_unique<ir::Record>(node.base_name);

        std::visit([&](auto const & body) {
            using T = std::decay_t<decltype(body)>;
            if constexpr (std::is_same_v<T, ast::Struct>) {
                generate_fields(body, evaluator, scope, node.context, node.fileid, builder, driver, 1, rec->fields, ir::SearchPolicies{}, rec->vocabs);
            } else if constexpr (std::is_same_v<T, ast::FormatRef>) {
                auto f = std::make_unique<ir::Field>(node.base_name, 1, 0);
                auto res = generate_format(node.base_name, body, evaluator, scope, node.context, node.fileid, builder, driver, rec->vocabs);
                f->format = std::move(res.format);
                f->refname = res.refname;
                f->refargs = res.refargs;
                for (auto & d : res.desc) f->desc.push_back(d);
                rec->fields.push_back(std::move(f));
            }
        }, decl.data.record);

        for (auto const & construct : decl.data.constructs) {
            std::visit([&](auto const & c) {
                using T = std::decay_t<decltype(c)>;
                if constexpr (std::is_same_v<T, ast::Annotate>) {
                    for (auto const & ann : c.data.annotations) {
                        if (!ann.data.path) {
                            auto val = evaluator.evaluate_expression(scope, ann.data.description, node.context);
                            if (auto * sv = std::get_if<std::string>(&val)) {
                                rec->desc.push_back(*sv);
                            }
                        }
                    }
                } else if constexpr (std::is_same_v<T, ast::Search>) {
                    lower_search(c, evaluator, scope, node.context, SearchScope::Record, driver, rec->search);
                }
            }, construct);
        }

        driver.records[mangled] = std::move(rec);
    }
}

// ============================================================================
// Assemble prompts from graph nodes
// ============================================================================

static void assemble_prompts(
    InstantiationGraph & graph,
    InstantiationGraphBuilder & builder,
    Evaluator & evaluator,
    Driver & driver
) {
    // File-scope search { }: the outermost cascade layer. Collect each file's
    // top-level search statements, keyed by fileid, so a prompt can start its
    // policy from its file's policy before applying its own.
    std::map<int, ir::SearchPolicies> file_policies;
    for (auto const & program : driver.programs) {
        int fid = program.data.fid;
        std::string fscope = std::to_string(fid) + "::";
        ir::VarMap fctx;
        for (auto const & stmt : program.data.statements) {
            std::visit([&](auto const & s) {
                using T = std::decay_t<decltype(s)>;
                if constexpr (std::is_same_v<T, ast::Search>) {
                    // File scope is outside any record; flow/queue are allowed.
                    lower_search(s, evaluator, fscope, fctx, SearchScope::File, driver, file_policies[fid]);
                }
            }, stmt);
        }
    }

    // Helper: resolve an unmangled prompt name to its mangled instance name
    auto resolve_prompt_ref = [&](std::string const & name) -> std::string {
        for (auto const & [m, n] : graph.nodes) {
            if (n.base_name == name) return m;
        }
        return name; // fallback: return as-is
    };

    for (auto & [mangled, node] : graph.nodes) {
        auto const * pmt_ref = std::get_if<std::reference_wrapper<const ast::Prompt>>(&node.ast);
        if (!pmt_ref) continue;

        auto const & decl = pmt_ref->get();
        auto scope = std::to_string(node.fileid) + "::" + node.base_name;
        auto pmt = std::make_unique<ir::Prompt>(node.base_name, mangled, node.arguments);

        // Policy cascade base = this file's file-scope search { }, then the
        // prompt's own search { } overrides it (innermost wins). flow/queue stay
        // on the prompt; text/enum/branch cascade down onto the fields.
        ir::SearchPolicies prompt_policy;
        auto fit = file_policies.find(node.fileid);
        if (fit != file_policies.end()) prompt_policy = fit->second;
        for (auto const & construct : decl.data.constructs) {
            std::visit([&](auto const & c) {
                using T = std::decay_t<decltype(c)>;
                if constexpr (std::is_same_v<T, ast::Search>) {
                    lower_search(c, evaluator, scope, node.context, SearchScope::Prompt, driver, prompt_policy);
                }
            }, construct);
        }
        pmt->search = prune_search(prompt_policy, {"flow", "queue"});
        ir::SearchPolicies field_enclosing = prune_search(prompt_policy, {"text", "enum", "branch"});

        if (decl.data.fields) {
            generate_fields(decl.data.fields.value(), evaluator, scope, node.context, node.fileid, builder, driver, 1, pmt->fields, field_enclosing, pmt->vocabs);
        }

        for (auto const & construct : decl.data.constructs) {
            std::visit([&](auto const & c) {
                using T = std::decay_t<decltype(c)>;

                if constexpr (std::is_same_v<T, ast::Flow>) {
                    for (auto const & edge : c.data.edges) {
                        auto const & objref = edge.data.prompt;
                        auto target_name = objref.data.name.data.name;
                        ir::VarMap kwargs;
                        for (auto const & assign : objref.data.config) {
                            auto k = assign.data.argument.data.name;
                            auto v = evaluator.evaluate_expression(scope, assign.data.value, node.context);
                            kwargs[k] = v;
                        }

                        std::string target_mangled;
                        auto resolved = builder.resolve(target_name, node.fileid);
                        if (resolved) {
                            auto target_ctx = builder.eval_context(*resolved, kwargs);
                            auto arguments = builder.extract_arguments(resolved->decl, target_ctx);
                            target_mangled = driver.mangle(resolved->name, arguments);
                        } else if (builder.is_python_symbol(target_name, node.fileid)) {
                            target_mangled = target_name;
                        } else {
                            driver.emit_error("Cannot resolve flow target '" + target_name + "'.", edge.location);
                            return;
                        }

                        auto fe = ir::FlowEdge(target_mangled);
                        fe.limit = eval_opt_int(edge.data.limit, evaluator, scope, node.context);
                        if (edge.data.label) {
                            auto lv = evaluator.evaluate_expression(scope, edge.data.label.value(), node.context);
                            if (auto * sv = std::get_if<std::string>(&lv)) fe.label = *sv;
                        }
                        pmt->flows.push_back(std::move(fe));
                    }

                } else if constexpr (std::is_same_v<T, ast::Channel>) {
                    for (auto const & link : c.data.links) {
                        auto target_steps = convert_path(link.data.target, evaluator, scope, node.context);
                        auto target_docpath = ir::DocPath(std::move(target_steps), false, std::nullopt);

                        std::visit([&](auto const & src) {
                            using S = std::decay_t<decltype(src)>;
                            if constexpr (std::is_same_v<S, std::monostate>) {
                                // skip
                            } else if constexpr (std::is_same_v<S, ast::Path>) {
                                auto source_steps = convert_path(src, evaluator, scope, node.context);
                                pmt->channels.push_back(ir::InputChannel(
                                    std::move(target_docpath),
                                    std::move(source_steps)
                                ));
                            } else if constexpr (std::is_same_v<S, ast::FieldRef>) {
                                std::optional<std::string> prompt_name;
                                if (src.data.prompt) {
                                    prompt_name = resolve_prompt_ref(src.data.prompt->data.name.data.name);
                                }
                                auto source_steps = convert_path(src.data.field, evaluator, scope, node.context);
                                auto dc = ir::DataflowChannel(
                                    std::move(target_docpath),
                                    std::move(prompt_name),
                                    std::move(source_steps)
                                );
                                dc.clauses = convert_clauses_from_link(link.data.clauses, evaluator, scope, node.context);
                                pmt->channels.push_back(std::move(dc));
                            } else if constexpr (std::is_same_v<S, ast::Call>) {
                                auto cc = ir::CallChannel(std::move(target_docpath));
                                auto const & objref = src.data.entry;
                                auto entry_name = objref.data.name.data.name;
                                auto resolved = builder.resolve(entry_name, node.fileid);
                                if (resolved) {
                                    ir::VarMap kwargs;
                                    for (auto const & assign : objref.data.config) {
                                        auto k = assign.data.argument.data.name;
                                        auto v = evaluator.evaluate_expression(scope, assign.data.value, node.context);
                                        kwargs[k] = v;
                                    }
                                    auto target_ctx = builder.eval_context(*resolved, kwargs);
                                    auto arguments = builder.extract_arguments(resolved->decl, target_ctx);
                                    cc.entry = driver.mangle(resolved->name, arguments);
                                } else if (builder.is_python_symbol(entry_name, node.fileid)) {
                                    cc.entry = entry_name;
                                    cc.extern_func = entry_name;
                                }
                                for (auto const & kwarg : src.data.arguments) {
                                    auto kw_name = kwarg.data.name.data.name;
                                    ir::Kwarg irk;
                                    std::visit([&](auto const & ks) {
                                        using KS = std::decay_t<decltype(ks)>;
                                        if constexpr (std::is_same_v<KS, ast::Path>) {
                                            irk.is_input = true;
                                            irk.path = convert_path(ks, evaluator, scope, node.context);
                                        } else if constexpr (std::is_same_v<KS, ast::FieldRef>) {
                                            if (ks.data.prompt) {
                                                irk.prompt = resolve_prompt_ref(ks.data.prompt->data.name.data.name);
                                            }
                                            irk.path = convert_path(ks.data.field, evaluator, scope, node.context);
                                        } else if constexpr (std::is_same_v<KS, ast::Expression>) {
                                            auto val = evaluator.evaluate_expression(scope, ks, node.context);
                                            if (auto * sv = std::get_if<std::string>(&val)) {
                                                irk.value = *sv;
                                            } else if (auto * iv = std::get_if<int>(&val)) {
                                                irk.value = std::to_string(*iv);
                                            } else if (auto * fv = std::get_if<float>(&val)) {
                                                irk.value = std::to_string(*fv);
                                            }
                                        }
                                    }, kwarg.data.source);
                                    irk.clauses = convert_clauses_from_link(kwarg.data.clauses, evaluator, scope, node.context);
                                    cc.kwargs[kw_name] = std::move(irk);
                                }
                                // Link-level clauses (ravel, bind, etc. on the channel itself)
                                cc.link_clauses = convert_clauses_from_link(link.data.clauses, evaluator, scope, node.context);
                                pmt->channels.push_back(std::move(cc));
                            } else if constexpr (std::is_same_v<S, ast::Expression>) {
                                // TODO: handle expression-valued channels
                            }
                        }, link.data.source);
                    }

                } else if constexpr (std::is_same_v<T, ast::Return>) {
                    ir::ReturnInfo ri;
                    if (c.data.label) {
                        ri.label = eval_alias(c.data.label.value(), evaluator, scope, node.context);
                    }
                    for (auto const & rf : c.data.fields) {
                        std::visit([&](auto const & src) {
                            using S = std::decay_t<decltype(src)>;
                            if constexpr (std::is_same_v<S, ast::Path>) {
                                auto steps = convert_path(src, evaluator, scope, node.context);
                                auto docpath = ir::DocPath(std::move(steps), false, std::nullopt);
                                auto ret = ir::ReturnField(std::move(docpath));
                                if (rf.data.alias) {
                                    ret.alias = eval_alias(rf.data.alias.value(), evaluator, scope, node.context);
                                }
                                ri.fields.push_back(std::move(ret));
                            } else if constexpr (std::is_same_v<S, ast::Expression>) {
                                auto val = evaluator.evaluate_expression(scope, src, node.context);
                                if (auto * sv = std::get_if<std::string>(&val)) {
                                    auto ret = ir::ReturnField(ir::DocPath({}, false, std::nullopt));
                                    ret.constant = *sv;
                                    if (rf.data.alias) {
                                        ret.alias = eval_alias(rf.data.alias.value(), evaluator, scope, node.context);
                                    }
                                    ri.fields.push_back(std::move(ret));
                                }
                            }
                        }, rf.data.source);
                    }
                    pmt->return_info = std::move(ri);

                } else if constexpr (std::is_same_v<T, ast::Annotate>) {
                    for (auto const & ann : c.data.annotations) {
                        auto val = evaluator.evaluate_expression(scope, ann.data.description, node.context);
                        if (auto * sv = std::get_if<std::string>(&val)) {
                            if (!ann.data.path) {
                                pmt->desc.push_back(*sv);
                            } else {
                                auto const & path = ann.data.path.value();
                                if (!path.data.steps.empty()) {
                                    auto field_name = path.data.steps.front().data.field.data.name;
                                    for (auto & f : pmt->fields) {
                                        if (f->name == field_name) {
                                            f->desc.push_back(*sv);
                                            break;
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
                // Search is handled up-front (prompt_policy) before field
                // generation, so it is intentionally not processed here.
            }, construct);
        }

        driver.prompts[mangled] = std::move(pmt);
    }
}

} // anonymous namespace

// ============================================================================
// Stage 5: Assemble IR from instantiation graph
// ============================================================================

std::optional<int> Driver::run_assemble() {
    InstantiationGraphBuilder builder(*this, *evaluator, graph);

    assemble_records(graph, builder, *evaluator, *this);
    assemble_prompts(graph, builder, *evaluator, *this);
    if (report_errors()) return 5;

    SPDLOG_LOGGER_DEBUG(autocog::log(), "IR assembled (#5): {} records, {} prompts",
                        records.size(), prompts.size());

    return std::nullopt;
}

} // namespace autocog::compiler::stl
