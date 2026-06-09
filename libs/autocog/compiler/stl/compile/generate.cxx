
#include "autocog/compiler/stl/driver.hxx"
#include "autocog/utilities/exception.hxx"
#include "autocog/data/sta.hxx"
#include "autocog/logging.hxx"

#include <algorithm>
#include <climits>
#include <functional>
#include <iostream>
#include <set>

namespace autocog::compiler::stl {

namespace data = autocog::data;

// ============================================================================
// Extract compact FieldInfo from IR
// ============================================================================

namespace {

// Local helpers over the data field/abstract tables: the data:: types are pure
// PODs, so the methods/free fns that lived on the runtime types live here.
static bool is_list(data::Field const & f) { return f.range.has_value(); }

static int abs_depth(data::Abstract const & a, std::vector<data::Field> const & fields) {
    return (a.field < 0) ? 0 : fields[a.field].depth;
}

// runtime/fta VocabExpr (unique_ptr operands) -> data VocabExpr (by-value
// operands). The Kind enums share order. Full unification is deferred.
static data::VocabExpr to_data_vocab(ir::VocabExpr const & ve) {
    data::VocabExpr out;
    out.kind = static_cast<data::VocabExpr::Kind>(ve.kind);
    out.strings = ve.strings;
    out.operands.reserve(ve.operands.size());
    for (auto const & op : ve.operands) out.operands.push_back(to_data_vocab(*op));
    return out;
}

// Convert an IR leaf format (variant alternative) to the STA FieldFormat.
// The struct (sub-fields) arm is handled by the caller (collect_fields), not here.
static data::FieldFormat extract_format(ir::Format const & fmt) {
    return std::visit([](auto const & f) -> data::FieldFormat {
        using T = std::decay_t<decltype(f)>;
        if constexpr (std::is_same_v<T, ir::Completion> ||
                      std::is_same_v<T, ir::Enum> ||
                      std::is_same_v<T, ir::Choice>) {
            // The IR leaf formats ARE the data leaf formats (shared types).
            return data::FieldFormat{f};
        } else {
            // struct (vector<Field>) -> record (no leaf format); handled by caller.
            return data::FieldFormat{std::monostate{}};
        }
    }, fmt);
}

// Convert resolved IR search policies to the STA SearchParams shape.
static data::SearchParams convert_policies(ir::SearchPolicies const & pol) {
    data::SearchParams out;
    for (auto const & [category, params] : pol) {
        for (auto const & [key, val] : params) {
            std::visit([&](auto const & v) { out.categories[category][key] = v; }, val);
        }
    }
    return out;
}

static void collect_fields(
    std::vector<ir::Field const *> & flat,
    std::vector<data::Field> & infos,
    std::vector<std::unique_ptr<ir::Field>> const & fields,
    int target_depth = 1   // what depth the first child should have
) {
    // Find minimum IR depth to compute offset
    int min_ir_depth = INT_MAX;
    for (auto const & fptr : fields) {
        if (fptr->depth < min_ir_depth) min_ir_depth = fptr->depth;
    }
    int depth_offset = (min_ir_depth < INT_MAX) ? target_depth - min_ir_depth : 0;

    for (auto const & fptr : fields) {
        auto const & f = *fptr;
        data::Field info;
        info.name = f.name;
        info.depth = f.depth + depth_offset;
        info.index = f.index;
        info.flat_index = static_cast<int>(flat.size());
        info.range = f.range;
        info.desc = f.desc;
        info.format_ref = f.refname;       // collapsed self-form record name, if any
        info.search = convert_policies(f.search);

        // Struct (container) field: monostate format, recurse into sub-fields.
        // Leaf field: extract the leaf format. The IR already collapsed
        // transparent/self-form records into leaves, so no resolve_transparent.
        if (f.is_struct()) {
            auto const & sub = std::get<std::vector<std::unique_ptr<ir::Field>>>(f.format);
            flat.push_back(&f);
            infos.push_back(std::move(info));
            collect_fields(flat, infos, sub, infos.back().depth + 1);
        } else {
            info.format = extract_format(f.format);
            flat.push_back(&f);
            infos.push_back(std::move(info));
        }
    }
}

// ============================================================================
// Build abstract states
// ============================================================================

// Matches Python: stack of lists, flow from parent to first child,
// exit from sibling to sibling, self-loop for lists.
static std::vector<data::Abstract> build_abstract(std::vector<data::Field> const & fields) {
    std::vector<data::Abstract> abstracts;
    abstracts.emplace_back(); // root at index 0 (field = -1)
    std::vector<std::vector<int>> stack = {{0}};

    for (int f = 0; f < static_cast<int>(fields.size()); ++f) {
        auto const & fld = fields[f];
        int idx = static_cast<int>(abstracts.size());
        data::Abstract st; st.field = f;
        abstracts.push_back(st);

        if (is_list(fld)) {
            abstracts[idx].flow = idx; // self-loop
        }

        int prev_idx = stack.back().back();

        if (fld.index == 0 && fld.depth == static_cast<int>(stack.size())) {
            // First child at new depth: parent flows into us
            abstracts[prev_idx].flow = idx;
            stack.push_back({});
        } else if (fld.depth < abs_depth(abstracts[prev_idx], fields)) {
            // Returning to shallower depth: wire intermediate exits, then pop
            for (int d = static_cast<int>(stack.size()) - 1; d > fld.depth; --d) {
                int deeper_last = stack[d].back();
                int shallower_last = stack[d - 1].back();
                abstracts[deeper_last].exit_ = shallower_last;
            }
            stack.resize(fld.depth + 1);
            abstracts[stack.back().back()].exit_ = idx;
        } else {
            // Same depth: prev exits to us
            abstracts[prev_idx].exit_ = idx;
        }

        stack.back().push_back(idx);
    }

    // Wire up stack: deeper last -> shallower last
    for (int i = 0; i + 1 < static_cast<int>(stack.size()); ++i) {
        int deeper_last = stack[i + 1].back();
        int shallower_last = stack[i].back();
        abstracts[deeper_last].exit_ = shallower_last;
    }

    return abstracts;
}


// ============================================================================
// Extract flow info from IR prompt
// ============================================================================

static std::map<std::string, data::Flow> extract_flows(ir::Prompt const & pmt) {
    std::map<std::string, data::Flow> flows;
    for (auto const & flow : pmt.flows) {
        std::string label = flow.label.value_or(flow.target_prompt);
        flows[label] = data::Flow{data::FlowControl{flow.target_prompt, flow.limit}};
    }
    if (pmt.return_info) {
        auto const & ri = pmt.return_info.value();
        std::string label = ri.label.value_or("return");
        data::FlowReturn rt;
        for (auto const & rf : ri.fields) {
            data::ReturnField srf;
            srf.alias = rf.alias.value_or("_");
            srf.path = rf.source.steps;   // shared PathStep; copy through
            rt.fields.push_back(std::move(srf));
        }
        flows[label] = data::Flow{std::move(rt)};
    }
    return flows;
}

// ============================================================================
// Extract channel descriptions from IR prompt
// ============================================================================

static data::PathStep convert_step(ir::PathStep const & step) {
    // IR and STA PathStep are the same shared type now; copy through faithfully.
    return step;
}

static std::vector<data::PathStep> convert_steps(std::vector<ir::PathStep> const & steps) {
    std::vector<data::PathStep> result;
    for (auto const & s : steps) result.push_back(convert_step(s));
    return result;
}

static std::vector<data::PathStep> convert_docpath(ir::DocPath const & dp) {
    return convert_steps(dp.steps);
}

static std::vector<data::Clause> convert_clauses(std::vector<ir::Clause> const & clauses) {
    // IR keeps a bare clause variant; wrap each into data::Clause at the boundary.
    std::vector<data::Clause> out;
    out.reserve(clauses.size());
    for (auto const & c : clauses) out.push_back(data::Clause{c});
    return out;
}

static std::vector<data::Channel> extract_channels(ir::Prompt const & pmt) {
    std::vector<data::Channel> channels;
    for (auto const & ch : pmt.channels) {
        std::visit([&](auto const & c) {
            using T = std::decay_t<decltype(c)>;
            if constexpr (std::is_same_v<T, ir::InputChannel>) {
                data::InputChannel ic;
                ic.target = convert_docpath(c.target);
                ic.source = convert_steps(c.source);
                channels.push_back(data::Channel{std::move(ic)});
            } else if constexpr (std::is_same_v<T, ir::DataflowChannel>) {
                data::DataflowChannel dc;
                dc.target = convert_docpath(c.target);
                dc.prompt = c.prompt;
                dc.source = convert_steps(c.source);
                dc.clauses = convert_clauses(c.clauses);
                channels.push_back(data::Channel{std::move(dc)});
            } else if constexpr (std::is_same_v<T, ir::CallChannel>) {
                data::CallChannel cc;
                cc.target = convert_docpath(c.target);
                cc.extern_func = c.extern_func;
                cc.entry = c.entry;
                for (auto const & [name, kwarg] : c.kwargs) {
                    data::ChannelKwarg ck;
                    ck.name = name;
                    ck.is_input = kwarg.is_input;
                    ck.prompt = kwarg.prompt;
                    ck.path = convert_steps(kwarg.path);
                    ck.value = kwarg.value;
                    ck.clauses = convert_clauses(kwarg.clauses);
                    cc.kwargs.push_back(std::move(ck));
                }
                // Link-level clauses
                cc.clauses = convert_clauses(c.link_clauses);
                channels.push_back(data::Channel{std::move(cc)});
            }
        }, ch);
    }
    return channels;
}

} // anonymous namespace

// ============================================================================
// Schema generation: collect inputs and outputs for each entry point
// ============================================================================

static data::SchemaField format_to_schema(data::FieldFormat const & fmt) {
    data::SchemaField s;
    s.type = "text";
    if (auto * cf = std::get_if<data::CompletionFormat>(&fmt.value)) {
        s.type = "text";
        if (cf->length > 0) s.max_length = cf->length;
    } else if (auto * ef = std::get_if<data::EnumFormat>(&fmt.value)) {
        s.type = "text";
        for (auto const & v : ef->values) {
            // Strip surrounding quotes if present
            if (v.size() >= 2 && v.front() == '"' && v.back() == '"') {
                s.enum_values.push_back(v.substr(1, v.size() - 2));
            } else {
                s.enum_values.push_back(v);
            }
        }
    } else if (std::holds_alternative<data::ChoiceFormat>(fmt.value)) {
        s.type = "text";
    } else if (std::holds_alternative<std::monostate>(fmt.value)) {
        s.type = "object";
    }
    return s;
}

static std::set<std::string> reachable_via_flow(
    std::map<std::string, data::Prompt> const & prompts,
    std::string const & entry
) {
    std::set<std::string> visited;
    std::vector<std::string> queue = {entry};
    while (!queue.empty()) {
        auto p = queue.back(); queue.pop_back();
        if (visited.count(p)) continue;
        visited.insert(p);
        auto it = prompts.find(p);
        if (it == prompts.end()) continue;
        for (auto const & [fname, fentry] : it->second.flows) {
            if (auto * ft = std::get_if<data::FlowControl>(&fentry.value)) {
                queue.push_back(ft->prompt);
            }
        }
    }
    return visited;
}

static void collect_inputs(
    std::map<std::string, data::Prompt> const & prompts,
    std::set<std::string> const & reachable,
    std::string const & entry_prompt,
    std::map<std::string, data::SchemaField> & inputs
) {
    for (auto const & pname : reachable) {
        auto it = prompts.find(pname);
        if (it == prompts.end()) continue;
        auto const & prompt = it->second;

        for (auto const & ch : prompt.channels) {
            if (auto * ic = std::get_if<data::InputChannel>(&ch.value)) {
                // Direct input channel
                if (ic->source.empty()) continue;
                std::string name = ic->source.back().name;
                std::string target_name = ic->target.empty() ? name : ic->target.back().name;

                // Find target field format and range
                data::SchemaField schema;
                schema.type = "text";
                for (auto const & f : prompt.fields) {
                    if (f.name == target_name) {
                        schema = format_to_schema(f.format);
                        // Array field?
                        if (f.range.has_value()) {
                            auto items = format_to_schema(f.format);
                            schema.type = "array";
                            schema.items_type = items.type;
                            schema.items_max_length = items.max_length;
                            schema.enum_values.clear();
                            auto [lo, hi] = f.range.value();
                            if (lo == hi) {
                                schema.length = lo;
                            } else {
                                schema.min_items = lo;
                                schema.max_items = hi;
                            }
                        }
                        break;
                    }
                }
                // InputChannel format takes precedence
                if (inputs.count(name) == 0 || schema.type != "text") {
                    schema.required = (pname == entry_prompt);
                    inputs[name] = schema;
                }

            } else if (auto * cc = std::get_if<data::CallChannel>(&ch.value)) {
                for (auto const & kw : cc->kwargs) {
                    if (kw.is_input && !kw.value.has_value()) {
                        if (kw.path.empty()) continue;
                        std::string name = kw.path.back().name;
                        if (inputs.count(name) == 0) {
                            data::SchemaField schema;
                            schema.type = "text";
                            schema.required = (pname == entry_prompt);
                            inputs[name] = schema;
                        }
                    }
                }
            }
        }
    }

    // Mark required: inputs used in entry prompt or on unconditional chain
    std::string current = entry_prompt;
    std::set<std::string> unconditional;
    while (!current.empty() && unconditional.count(current) == 0) {
        unconditional.insert(current);
        auto it = prompts.find(current);
        if (it == prompts.end()) break;
        // Count control flows
        std::string next;
        int control_count = 0;
        for (auto const & [fname, fentry] : it->second.flows) {
            if (auto * ft = std::get_if<data::FlowControl>(&fentry.value)) {
                control_count++;
                next = ft->prompt;
            }
        }
        current = (control_count == 1) ? next : "";
    }
    // Inputs used in unconditional prompts are required
    for (auto & [name, schema] : inputs) {
        for (auto const & pname : reachable) {
            if (unconditional.count(pname) == 0) continue;
            auto it = prompts.find(pname);
            if (it == prompts.end()) continue;
            for (auto const & ch : it->second.channels) {
                bool found = false;
                if (auto * ic = std::get_if<data::InputChannel>(&ch.value)) {
                    if (!ic->source.empty() && ic->source.back().name == name) found = true;
                } else if (auto * cc = std::get_if<data::CallChannel>(&ch.value)) {
                    for (auto const & kw : cc->kwargs) {
                        if (kw.is_input && !kw.path.empty() && kw.path.back().name == name) found = true;
                    }
                }
                if (found) { schema.required = true; break; }
            }
        }
    }
}

static void collect_outputs(
    std::map<std::string, data::Prompt> const & prompts,
    std::set<std::string> const & reachable,
    std::map<std::string, data::SchemaField> & outputs
) {
    for (auto const & pname : reachable) {
        auto it = prompts.find(pname);
        if (it == prompts.end()) continue;
        auto const & prompt = it->second;

        // Check for return flows
        for (auto const & [fname, fentry] : prompt.flows) {
            auto * rt = std::get_if<data::FlowReturn>(&fentry.value);
            if (!rt) continue;
            for (auto const & rf : rt->fields) {
                std::string name = rf.alias.empty() ? "_" : rf.alias;
                data::SchemaField schema;
                schema.type = "text";
                if (!rf.path.empty()) {
                    std::string field_name = rf.path.back().name;
                    for (auto const & f : prompt.fields) {
                        if (f.name == field_name) {
                            schema = format_to_schema(f.format);
                            if (f.range.has_value()) {
                                auto items = format_to_schema(f.format);
                                schema.type = "array";
                                schema.items_type = items.type;
                                schema.items_max_length = items.max_length;
                                schema.enum_values.clear();
                                auto [lo, hi] = f.range.value();
                                if (lo == hi) schema.length = lo;
                                else { schema.min_items = lo; schema.max_items = hi; }
                            }
                            break;
                        }
                    }
                }
                outputs[name] = schema;
            }
        }

        // Terminal prompts (no flows): output is the full frame
        if (prompt.flows.empty()) {
            for (auto const & f : prompt.fields) {
                if (std::holds_alternative<std::monostate>(f.format.value)) continue; // skip records
                data::SchemaField schema = format_to_schema(f.format);
                if (f.range.has_value()) {
                    auto items = format_to_schema(f.format);
                    schema.type = "array";
                    schema.items_type = items.type;
                    schema.items_max_length = items.max_length;
                    schema.enum_values.clear();
                    auto [lo, hi] = f.range.value();
                    if (lo == hi) schema.length = lo;
                    else { schema.min_items = lo; schema.max_items = hi; }
                }
                outputs[f.name] = schema;
            }
        }
    }
}

// ============================================================================
// Stage 6: Generate STA
// ============================================================================

std::optional<int> Driver::run_generate() {

    // Populate Python imports from symbol table
    for (auto const & [key, sym] : tables.symbols) {
        if (auto * ps = std::get_if<PythonSymbol>(&sym)) {
            auto pos = key.rfind("::");
            auto name = (pos != std::string::npos) ? key.substr(pos + 2) : key;
            auto & target = ps->target.data.name.data.name;
            sta.python_imports[name] = data::PythonImport{ps->filename, target};
        }
    }

    for (auto const & [mangled, pmt_ptr] : prompts) {
        auto const & pmt = *pmt_ptr;
        data::Prompt pstas;
        pstas.name = pmt.mangled_name;
        pstas.desc = pmt.desc;

        // Carry the prompt-scope search params verbatim (IR Value -> STA
        // SearchValue; identical scalar shapes). Per-field/per-state resolution
        // is applied later at instantiation.
        for (auto const & [category, params] : pmt.search) {
            for (auto const & [key, val] : params) {
                std::visit([&](auto const & v) {
                    pstas.search.categories[category][key] = v;
                }, val);
            }
        }

        // Carry the vocab table: vocab_<hash> -> resolved expression tree.
        for (auto const & [key, ve] : pmt.vocabs) {
            pstas.vocabs[key] = to_data_vocab(ve);
        }

    SPDLOG_LOGGER_DEBUG(autocog::log(), "STA: building {} ({} IR fields)", mangled, pmt.fields.size());

        // Collect flat field list + compact info
        std::vector<ir::Field const *> flat_fields;
        collect_fields(flat_fields, pstas.fields, pmt.fields);

        // Add implicit "next" field for flow control
        // This encodes flow labels as an enum so the state graph
        // handles flow selection like any other field.
        {
            auto flow_map = extract_flows(pmt);
            if (!flow_map.empty()) {
                data::Field next_field;
                next_field.name = "next";
                next_field.depth = 1;
                next_field.index = static_cast<int>(pstas.fields.size());
                next_field.flat_index = static_cast<int>(pstas.fields.size());
                data::EnumFormat ef;
                for (auto const & [label, _] : flow_map) {
                    ef.values.push_back(label);
                }
                next_field.format.value = ef;
                next_field.desc.push_back("the next step");
                pstas.fields.push_back(std::move(next_field));
            }
        }

    SPDLOG_LOGGER_DEBUG(autocog::log(), "  flat: {} fields", pstas.fields.size());

        // Build abstract states (the serialized state graph). Concretization
        // (index-expansion) is performed by ista at instantiation time, using
        // the actual content — it is no longer done here.
        pstas.abstracts = build_abstract(pstas.fields);

    SPDLOG_LOGGER_DEBUG(autocog::log(), "  abstract: {} states", pstas.abstracts.size());

        // Extract flows
        pstas.flows = extract_flows(pmt);

        // Extract channels
        pstas.channels = extract_channels(pmt);

        sta.prompts[mangled] = std::move(pstas);
    }

    if (report_errors()) return 6;

    SPDLOG_LOGGER_INFO(autocog::log(), "STA generated (#6): {} prompts", sta.prompts.size());

    // Build entry points with schemas (must be after prompts are populated)
    for (auto const & [entry_name, mangled] : entry_point_map) {
        data::EntryPoint ep;
        ep.prompt = mangled;

        auto reachable = reachable_via_flow(sta.prompts, mangled);
        collect_inputs(sta.prompts, reachable, mangled, ep.inputs);
        collect_outputs(sta.prompts, reachable, ep.outputs);

        sta.entry_points[entry_name] = std::move(ep);
    }

    // STA is a root artifact: empty provenance, content-only hash.
    sta.finalize();

    return std::nullopt;
}

} // namespace autocog::compiler::stl
