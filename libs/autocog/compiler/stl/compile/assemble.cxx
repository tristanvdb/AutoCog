
#include "autocog/compiler/stl/driver.hxx"
#include "autocog/compiler/stl/evaluate.hxx"
#include "autocog/compiler/stl/instantiation-graph.hxx"

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

static std::vector<ir::PathStep> convert_path(
    ast::Path const & path,
    Evaluator & evaluator,
    std::string const & scope,
    ir::VarMap & ctx
) {
    std::vector<ir::PathStep> steps;
    for (auto const & step : path.data.steps) {
        auto name = step.data.field.data.name;
        ir::Range range = std::nullopt;
        auto lower = eval_opt_int(step.data.lower, evaluator, scope, ctx);
        auto upper = eval_opt_int(step.data.upper, evaluator, scope, ctx);
        if (lower && upper) {
            range = std::make_pair(*lower, *upper);
        } else if (lower) {
            range = std::make_pair(*lower, *lower);
        }
        steps.emplace_back(name, range);
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
static std::unique_ptr<ir::Format> generate_format(
    std::string const & field_name,
    ast::FormatRef const & fref,
    Evaluator & evaluator,
    std::string const & scope,
    ir::VarMap & ctx,
    int fileid,
    InstantiationGraphBuilder & builder,
    Driver & driver
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
    std::vector<std::unique_ptr<ir::Field>> & out
) {
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
            if constexpr (std::is_same_v<T, std::monostate>) {
                // no format
            } else if constexpr (std::is_same_v<T, ast::FormatRef>) {
                f->format = generate_format(name, type, evaluator, scope, ctx, fileid, builder, driver);
            } else if constexpr (std::is_same_v<T, ast::Struct>) {
                auto rf = std::make_unique<ir::RecordFormat>(name);
                generate_fields(type, evaluator, scope, ctx, fileid, builder, driver, depth + 1, rf->fields);
                f->format = std::move(rf);
            }
        }, field.data.type);

        out.push_back(std::move(f));
    }
}

static std::unique_ptr<ir::Format> generate_format(
    std::string const & field_name,
    ast::FormatRef const & fref,
    Evaluator & evaluator,
    std::string const & scope,
    ir::VarMap & ctx,
    int fileid,
    InstantiationGraphBuilder & builder,
    Driver & driver
) {
    return std::visit([&](auto const & type) -> std::unique_ptr<ir::Format> {
        using T = std::decay_t<decltype(type)>;

        if constexpr (std::is_same_v<T, std::monostate>) {
            return nullptr;

        } else if constexpr (std::is_same_v<T, ast::Text>) {
            auto fmt = std::make_unique<ir::Completion>(field_name);
            for (auto const & assign : fref.data.kwargs) {
                auto k = assign.data.argument.data.name;
                auto v = evaluator.evaluate_expression(scope, assign.data.value, ctx);
                if (k == "length" && std::holds_alternative<int>(v)) fmt->length = std::get<int>(v);
                else if (k == "threshold" && std::holds_alternative<float>(v)) fmt->threshold = std::get<float>(v);
                else if (k == "beams" && std::holds_alternative<int>(v)) fmt->beams = std::get<int>(v);
                else if (k == "ahead" && std::holds_alternative<int>(v)) fmt->ahead = std::get<int>(v);
                else if (k == "width" && std::holds_alternative<int>(v)) fmt->width = std::get<int>(v);
            }
            return fmt;

        } else if constexpr (std::is_same_v<T, ast::Enum>) {
            auto fmt = std::make_unique<ir::Enum>(field_name);
            for (auto const & s : type.data.enumerators) {
                fmt->values.push_back(s.data.value);
            }
            return fmt;

        } else if constexpr (std::is_same_v<T, ast::Choice>) {
            std::string mode = (type.data.mode == ast::ChoiceKind::Select) ? "select" : "repeat";
            auto fmt = std::make_unique<ir::Choice>(field_name, mode);
            auto steps = convert_path(type.data.source, evaluator, scope, ctx);
            fmt->path = ir::DocPath(std::move(steps), false, std::nullopt);
            for (auto const & assign : fref.data.kwargs) {
                auto k = assign.data.argument.data.name;
                auto v = evaluator.evaluate_expression(scope, assign.data.value, ctx);
                if (k == "width" && std::holds_alternative<int>(v)) fmt->width = std::get<int>(v);
            }
            return fmt;

        } else if constexpr (std::is_same_v<T, ast::Identifier>) {
            auto rec_name = type.data.name;
            ir::VarMap kwargs;
            for (auto const & assign : fref.data.kwargs) {
                auto k = assign.data.argument.data.name;
                auto v = evaluator.evaluate_expression(scope, assign.data.value, ctx);
                kwargs[k] = v;
            }
            auto resolved = builder.resolve(rec_name, fileid);
            if (!resolved) return nullptr;

            auto target_ctx = builder.eval_context(*resolved, kwargs);
            auto target_scope = std::to_string(resolved->fileid) + "::" + resolved->name;

            auto const * rec_ref = std::get_if<std::reference_wrapper<const ast::Record>>(&resolved->decl);
            if (!rec_ref) return nullptr;

            auto const & rec_decl = rec_ref->get();
            auto rf = std::make_unique<ir::RecordFormat>(field_name);

            std::visit([&](auto const & body) {
                using B = std::decay_t<decltype(body)>;
                if constexpr (std::is_same_v<B, ast::Struct>) {
                    generate_fields(body, evaluator, target_scope, target_ctx, resolved->fileid, builder, driver, 1, rf->fields);
                } else if constexpr (std::is_same_v<B, ast::FormatRef>) {
                    auto f = std::make_unique<ir::Field>(resolved->name, 1, 0);
                    f->format = generate_format(resolved->name, body, evaluator, target_scope, target_ctx, resolved->fileid, builder, driver);
                    rf->fields.push_back(std::move(f));
                }
            }, rec_decl.data.record);

            for (auto const & construct : rec_decl.data.constructs) {
                std::visit([&](auto const & c) {
                    using C = std::decay_t<decltype(c)>;
                    if constexpr (std::is_same_v<C, ast::Annotate>) {
                        for (auto const & ann : c.data.annotations) {
                            if (!ann.data.path) {
                                auto val = evaluator.evaluate_expression(target_scope, ann.data.description, target_ctx);
                                if (auto * sv = std::get_if<std::string>(&val)) {
                                    rf->desc.push_back(*sv);
                                }
                            }
                        }
                    }
                }, construct);
            }

            return rf;
        }

        return nullptr;
    }, fref.data.type);
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
                generate_fields(body, evaluator, scope, node.context, node.fileid, builder, driver, 1, rec->fields);
            } else if constexpr (std::is_same_v<T, ast::FormatRef>) {
                auto f = std::make_unique<ir::Field>(node.base_name, 1, 0);
                f->format = generate_format(node.base_name, body, evaluator, scope, node.context, node.fileid, builder, driver);
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
    for (auto & [mangled, node] : graph.nodes) {
        auto const * pmt_ref = std::get_if<std::reference_wrapper<const ast::Prompt>>(&node.ast);
        if (!pmt_ref) continue;

        auto const & decl = pmt_ref->get();
        auto scope = std::to_string(node.fileid) + "::" + node.base_name;
        auto pmt = std::make_unique<ir::Prompt>(node.base_name, mangled, node.arguments);

        if (decl.data.fields) {
            generate_fields(decl.data.fields.value(), evaluator, scope, node.context, node.fileid, builder, driver, 1, pmt->fields);
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
                                    prompt_name = src.data.prompt->data.name.data.name;
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
                                                irk.prompt = ks.data.prompt->data.name.data.name;
                                            }
                                            irk.path = convert_path(ks.data.field, evaluator, scope, node.context);
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

#if !defined(NDEBUG)
    std::cerr << "After assembling IR (#5):" << std::endl;
    std::cerr << "  " << records.size() << " records, "
              << prompts.size() << " prompts" << std::endl;
    for (auto const & [name, rec] : records) {
        std::cerr << "    Record: " << name << " (" << rec->fields.size() << " fields)" << std::endl;
    }
    for (auto const & [name, pmt] : prompts) {
        std::cerr << "    Prompt: " << name << " (" << pmt->fields.size() << " fields, "
                  << pmt->channels.size() << " channels, " << pmt->flows.size() << " flows)" << std::endl;
    }
#endif

    return std::nullopt;
}

} // namespace autocog::compiler::stl
