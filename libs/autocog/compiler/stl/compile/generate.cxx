
#include "autocog/compiler/stl/driver.hxx"
#include "autocog/utilities/exception.hxx"
#include "autocog/runtime/sta/state.hxx"
#include "autocog/logging.hxx"

#include <algorithm>
#include <climits>
#include <functional>
#include <iostream>
#include <set>

namespace autocog::compiler::stl {

namespace sta = autocog::runtime::sta;

// ============================================================================
// Extract compact FieldInfo from IR
// ============================================================================

namespace {

// Convert an IR leaf format (variant alternative) to the STA FieldFormat.
// The struct (sub-fields) arm is handled by the caller (collect_fields), not here.
static sta::FieldFormat extract_format(ir::Format const & fmt) {
    return std::visit([](auto const & f) -> sta::FieldFormat {
        using T = std::decay_t<decltype(f)>;
        if constexpr (std::is_same_v<T, ir::Completion>) {
            sta::CompletionFormat cf;
            cf.length = f.length;
            cf.within = f.within;
            return cf;
        } else if constexpr (std::is_same_v<T, ir::Enum>) {
            sta::EnumFormat ef;
            ef.values = f.values;
            return ef;
        } else if constexpr (std::is_same_v<T, ir::Choice>) {
            sta::ChoiceFormat chf;
            chf.mode = f.mode;
            for (auto const & step : f.path.steps) {
                chf.path.emplace_back(step.name, step.range);
            }
            return chf;
        } else {
            // struct (vector<Field>) → record (no leaf format); handled by caller.
            return std::monostate{};
        }
    }, fmt);
}

// Convert resolved IR search policies to the STA SearchParams shape.
static sta::SearchParams convert_policies(ir::SearchPolicies const & pol) {
    sta::SearchParams out;
    for (auto const & [category, params] : pol) {
        for (auto const & [key, val] : params) {
            std::visit([&](auto const & v) { out[category][key] = v; }, val);
        }
    }
    return out;
}

static void collect_fields(
    std::vector<ir::Field const *> & flat,
    std::vector<sta::FieldInfo> & infos,
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
        sta::FieldInfo info;
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

struct AbstractState {
    int field;  // index into fields, -1 for root
    int flow;   // index into abstracts, -1 for none
    int exit_;  // index into abstracts, -1 for none

    AbstractState() : field(-1), flow(-1), exit_(-1) {}
    explicit AbstractState(int f) : field(f), flow(-1), exit_(-1) {}

    int depth(std::vector<sta::FieldInfo> const & fields) const {
        return (field < 0) ? 0 : fields[field].depth;
    }

    std::string tag(std::vector<sta::FieldInfo> const & fields) const {
        return (field < 0) ? "root" : fields[field].tag();
    }
};

// Matches Python: stack of lists, flow from parent to first child,
// exit from sibling to sibling, self-loop for lists.
static std::vector<AbstractState> build_abstract(std::vector<sta::FieldInfo> const & fields) {
    std::vector<AbstractState> abstracts;
    abstracts.emplace_back(); // root at index 0
    std::vector<std::vector<int>> stack = {{0}};

    for (int f = 0; f < static_cast<int>(fields.size()); ++f) {
        auto const & fld = fields[f];
        int idx = static_cast<int>(abstracts.size());
        abstracts.emplace_back(f);

        if (fld.is_list()) {
            abstracts[idx].flow = idx; // self-loop
        }

        int prev_idx = stack.back().back();

        if (fld.index == 0 && fld.depth == static_cast<int>(stack.size())) {
            // First child at new depth: parent flows into us
            abstracts[prev_idx].flow = idx;
            stack.push_back({});
        } else if (fld.depth < abstracts[prev_idx].depth(fields)) {
            // Returning to shallower depth: wire intermediate exits, then pop
            // Each dropped level's last element exits to the next shallower level's last
            for (int d = static_cast<int>(stack.size()) - 1; d > fld.depth; --d) {
                int deeper_last = stack[d].back();
                int shallower_last = stack[d - 1].back();
                abstracts[deeper_last].exit_ = shallower_last;
            }
            stack.resize(fld.depth + 1);
            // Last element at target depth exits to the new field
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
// Build concrete states
// ============================================================================

static std::string make_tag(AbstractState const & abs,
                            std::vector<sta::FieldInfo> const & fields,
                            std::vector<int> const & indices) {
    std::string base = abs.tag(fields);
    std::string idx_str;
    for (int i = 1; i < static_cast<int>(indices.size()); ++i) {
        if (i > 1) idx_str += "_";
        idx_str += std::to_string(indices[i]);
    }
    return base + "@" + idx_str;
}

static std::string build_concrete_rec(
    std::vector<AbstractState> const & abstracts,
    std::vector<sta::FieldInfo> const & fields,
    std::map<std::string, sta::ConcreteState> & states,
    int abs_idx,
    std::vector<int> indices
) {
    auto const & abs = abstracts[abs_idx];
    int depth = abs.depth(fields);
    int count = indices.back();
    std::string ctag = make_tag(abs, fields, indices);

    if (states.count(ctag)) return ctag;

    bool is_list, is_record;
    if (abs.field >= 0) {
        is_list = fields[abs.field].is_list();
        is_record = fields[abs.field].is_record();
    } else {
        is_list = false;
        is_record = true;
    }

    bool can_flow, can_exit;
    if (is_list) {
        can_flow = count < fields[abs.field].range->second;
        can_exit = count >= fields[abs.field].range->first;
    } else if (is_record) {
        can_flow = count == 0;
        can_exit = count > 0;
    } else {
        can_flow = true;
        can_exit = true;
    }
    can_flow = can_flow && (abs.flow >= 0);
    can_exit = can_exit && (abs.exit_ >= 0);

    sta::ConcreteState cs;
    cs.tag = ctag;
    cs.field = abs.field;
    cs.indices = std::vector<int>(indices.begin() + 1, indices.end());
    states[ctag] = cs;

    if (can_flow) {
        auto new_indices = indices;
        auto const & flow_abs = abstracts[abs.flow];
        if (depth < flow_abs.depth(fields)) {
            new_indices.push_back(0);
        } else {
            new_indices.back() += 1;
        }
        auto next = build_concrete_rec(abstracts, fields, states, abs.flow, new_indices);
        states[ctag].flows.push_back(next);
    }

    if (can_exit) {
        auto new_indices = indices;
        auto const & exit_abs = abstracts[abs.exit_];
        int delta = depth - exit_abs.depth(fields);
        if (delta > 0) {
            new_indices.resize(new_indices.size() - delta);
            new_indices.back() += 1;
        } else {
            new_indices.back() = 0;
        }
        auto next = build_concrete_rec(abstracts, fields, states, abs.exit_, new_indices);
        states[ctag].exits.push_back(next);
    }

    return ctag;
}

// ============================================================================
// Post-process concrete states (matches Python build_concrete)
// ============================================================================

static std::vector<std::string> get_sequence(std::map<std::string, sta::ConcreteState> const & states) {
    std::vector<std::string> seq;
    std::string cur = "root@";
    while (true) {
        seq.push_back(cur);
        auto it = states.find(cur);
        if (it == states.end() || it->second.successors.empty()) break;
        cur = it->second.successors[0];
        if (cur == "root@") break;
    }
    return seq;
}

static void post_process(
    std::vector<sta::FieldInfo> const & fields,
    std::map<std::string, sta::ConcreteState> & states
) {
    // Step 1: merge flows/exits into successors
    for (auto & [tag, state] : states) {
        if (!state.flows.empty()) {
            state.successors.push_back(state.flows[0]);
            state.flows.clear();  // exits intentionally kept for reversed_exits
        } else if (!state.exits.empty()) {
            if (state.exits[0] != "root@") {
                state.successors.push_back(state.exits[0]);
                state.exits.clear();
            }
            // exits to root@ intentionally kept for list tail pruning
        }
    }

    auto sequence = get_sequence(states);

    // Build reversed exits
    std::map<std::string, std::vector<std::string>> reversed_exits;
    for (auto const & stag : sequence) {
        for (auto const & e : states[stag].exits) {
            if (e == "root@") continue;
            auto & vec = reversed_exits[e];
            if (std::find(vec.begin(), vec.end(), stag) == vec.end()) {
                vec.push_back(stag);
            }
        }
    }

    // Step 2: prune list tails
    std::vector<std::string> delete_set;
    bool prev_deleted = false;
    std::string last_kept_tag;
    for (size_t s = 0; s < sequence.size(); ++s) {
        auto const & stag = sequence[s];
        auto & state = states[stag];

        bool is_list_tail = false;
        if (state.field >= 0) {
            auto const & fld = fields[state.field];
            if (fld.is_list() && !state.indices.empty()) {
                is_list_tail = state.indices.back() >= fld.range->second;
            }
        }

        if (is_list_tail) {
            delete_set.push_back(stag);
            if (!state.exits.empty() && state.exits[0] == "root@") {
                if (s > 0) {
                    states[sequence[s - 1]].successors.clear();
                }
            } else if (reversed_exits.count(stag)) {
                if (state.successors.size() != 1) {
                    throw autocog::utilities::InternalError(
                        "List tail '" + stag + "' is the target of exit edges but has "
                        + std::to_string(state.successors.size()) + " successors. "
                        "A variable-length list cannot be the last field in its enclosing scope.");
                }
                auto succ = state.successors[0];
                for (auto const & e : reversed_exits[stag]) {
                    auto & vec = reversed_exits[succ];
                    if (std::find(vec.begin(), vec.end(), e) == vec.end()) {
                        vec.push_back(e);
                    }
                }
                reversed_exits.erase(stag);
            }
            prev_deleted = true;
        } else {
            if (prev_deleted && !last_kept_tag.empty()) {
                states[last_kept_tag].successors = {stag};
            }
            last_kept_tag = stag;
            prev_deleted = false;
        }
    }

    for (auto const & tag : delete_set) {
        states.erase(tag);
    }

    // Step 3: clear all exits, reassign from reversed_exits
    for (auto & [tag, state] : states) {
        state.exits.clear();
    }
    for (auto const & [sink, srcs] : reversed_exits) {
        for (auto const & src : srcs) {
            if (states.count(src)) {
                states[src].exits.push_back(sink);
            }
        }
    }

    // Step 4: propagate exit edges — exit closure on predecessors
    sequence = get_sequence(states);
    for (size_t s = 1; s < sequence.size(); ++s) {
        auto const & stag = sequence[s];
        auto & state = states[stag];
        if (state.exits.empty()) continue;

        auto & pred = states[sequence[s - 1]];

        // Exit closure
        std::vector<std::string> closure;
        std::vector<std::string> todo = state.exits;
        while (!todo.empty()) {
            std::vector<std::string> next;
            for (auto const & t : todo) {
                if (std::find(closure.begin(), closure.end(), t) == closure.end()) {
                    closure.push_back(t);
                    if (states.count(t)) {
                        for (auto const & e : states[t].exits) {
                            next.push_back(e);
                        }
                    }
                }
            }
            todo = next;
        }

        for (auto const & e : closure) {
            if (std::find(pred.successors.begin(), pred.successors.end(), e) == pred.successors.end()) {
                pred.successors.push_back(e);
            }
        }
        state.exits.clear();
    }

    // Clean up remaining flows/exits
    for (auto & [tag, state] : states) {
        state.flows.clear();
        state.exits.clear();
    }
}

// ============================================================================
// Extract flow info from IR prompt
// ============================================================================

static std::map<std::string, sta::FlowEntry> extract_flows(ir::Prompt const & pmt) {
    std::map<std::string, sta::FlowEntry> flows;
    for (auto const & flow : pmt.flows) {
        std::string label = flow.label.value_or(flow.target_prompt);
        flows[label] = sta::FlowTarget{flow.target_prompt, flow.limit};
    }
    if (pmt.return_info) {
        auto const & ri = pmt.return_info.value();
        std::string label = ri.label.value_or("return");
        sta::ReturnTarget rt;
        for (auto const & rf : ri.fields) {
            sta::ReturnField srf;
            srf.alias = rf.alias.value_or("_");
            for (auto const & step : rf.source.steps) {
                srf.path.emplace_back(step.name, step.range.has_value()
                    ? std::optional<int>(step.range->first) : std::nullopt);
            }
            rt.fields.push_back(std::move(srf));
        }
        flows[label] = std::move(rt);
    }
    return flows;
}

// ============================================================================
// Extract channel descriptions from IR prompt
// ============================================================================

static sta::PathStep convert_step(ir::PathStep const & step) {
    sta::PathStep ps;
    ps.name = step.name;
    if (step.range) ps.index = step.range->first;
    return ps;
}

static std::vector<sta::PathStep> convert_steps(std::vector<ir::PathStep> const & steps) {
    std::vector<sta::PathStep> result;
    for (auto const & s : steps) result.push_back(convert_step(s));
    return result;
}

static std::vector<sta::PathStep> convert_docpath(ir::DocPath const & dp) {
    return convert_steps(dp.steps);
}

static std::vector<sta::Clause> convert_clauses(std::vector<ir::Clause> const & clauses) {
    std::vector<sta::Clause> result;
    for (auto const & clause : clauses) {
        std::visit([&](auto const & c) {
            using T = std::decay_t<decltype(c)>;
            if constexpr (std::is_same_v<T, ir::BindClause>) {
                sta::BindClause bc;
                bc.source = convert_steps(c.source);
                bc.target = convert_steps(c.target);
                result.push_back(std::move(bc));
            } else if constexpr (std::is_same_v<T, ir::RavelClause>) {
                sta::RavelClause rc;
                rc.depth = c.depth.value_or(1);
                rc.target = convert_steps(c.target);
                result.push_back(std::move(rc));
            } else if constexpr (std::is_same_v<T, ir::WrapClause>) {
                sta::WrapClause wc;
                wc.target = convert_steps(c.target);
                result.push_back(std::move(wc));
            } else if constexpr (std::is_same_v<T, ir::PruneClause>) {
                sta::PruneClause pc;
                pc.target = convert_steps(c.target);
                result.push_back(std::move(pc));
            } else if constexpr (std::is_same_v<T, ir::MappedClause>) {
                sta::MappedClause mc;
                mc.target = convert_steps(c.target);
                result.push_back(std::move(mc));
            }
        }, clause);
    }
    return result;
}

static std::vector<sta::Channel> extract_channels(ir::Prompt const & pmt) {
    std::vector<sta::Channel> channels;
    for (auto const & ch : pmt.channels) {
        std::visit([&](auto const & c) {
            using T = std::decay_t<decltype(c)>;
            if constexpr (std::is_same_v<T, ir::InputChannel>) {
                sta::InputChannel ic;
                ic.target = convert_docpath(c.target);
                ic.source = convert_steps(c.source);
                channels.push_back(std::move(ic));
            } else if constexpr (std::is_same_v<T, ir::DataflowChannel>) {
                sta::DataflowChannel dc;
                dc.target = convert_docpath(c.target);
                dc.prompt = c.prompt;
                dc.source = convert_steps(c.source);
                dc.clauses = convert_clauses(c.clauses);
                channels.push_back(std::move(dc));
            } else if constexpr (std::is_same_v<T, ir::CallChannel>) {
                sta::CallChannel cc;
                cc.target = convert_docpath(c.target);
                cc.extern_func = c.extern_func;
                cc.entry = c.entry;
                for (auto const & [name, kwarg] : c.kwargs) {
                    sta::ChannelKwarg ck;
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
                // Also convert legacy binds to bind clauses
                if (c.binds) {
                    for (auto const & [target_name, source_path] : *c.binds) {
                        sta::BindClause bc;
                        bc.source = convert_steps(source_path);
                        bc.target = {{target_name, std::nullopt}};
                        cc.clauses.push_back(std::move(bc));
                    }
                }
                channels.push_back(std::move(cc));
            }
        }, ch);
    }
    return channels;
}

} // anonymous namespace

// ============================================================================
// Schema generation: collect inputs and outputs for each entry point
// ============================================================================

static sta::SchemaField format_to_schema(sta::FieldFormat const & fmt) {
    sta::SchemaField s;
    s.type = "text";
    if (auto * cf = std::get_if<sta::CompletionFormat>(&fmt)) {
        s.type = "text";
        if (cf->length > 0) s.max_length = cf->length;
    } else if (auto * ef = std::get_if<sta::EnumFormat>(&fmt)) {
        s.type = "text";
        for (auto const & v : ef->values) {
            // Strip surrounding quotes if present
            if (v.size() >= 2 && v.front() == '"' && v.back() == '"') {
                s.enum_values.push_back(v.substr(1, v.size() - 2));
            } else {
                s.enum_values.push_back(v);
            }
        }
    } else if (std::holds_alternative<sta::ChoiceFormat>(fmt)) {
        s.type = "text";
    } else if (std::holds_alternative<std::monostate>(fmt)) {
        s.type = "object";
    }
    return s;
}

static std::set<std::string> reachable_via_flow(
    std::map<std::string, sta::PromptSTA> const & prompts,
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
            if (auto * ft = std::get_if<sta::FlowTarget>(&fentry)) {
                queue.push_back(ft->prompt);
            }
        }
    }
    return visited;
}

static void collect_inputs(
    std::map<std::string, sta::PromptSTA> const & prompts,
    std::set<std::string> const & reachable,
    std::string const & entry_prompt,
    std::map<std::string, sta::SchemaField> & inputs
) {
    for (auto const & pname : reachable) {
        auto it = prompts.find(pname);
        if (it == prompts.end()) continue;
        auto const & prompt = it->second;

        for (auto const & ch : prompt.channels) {
            if (auto * ic = std::get_if<sta::InputChannel>(&ch)) {
                // Direct input channel
                if (ic->source.empty()) continue;
                std::string name = ic->source.back().name;
                std::string target_name = ic->target.empty() ? name : ic->target.back().name;

                // Find target field format and range
                sta::SchemaField schema;
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

            } else if (auto * cc = std::get_if<sta::CallChannel>(&ch)) {
                for (auto const & kw : cc->kwargs) {
                    if (kw.is_input && !kw.value.has_value()) {
                        if (kw.path.empty()) continue;
                        std::string name = kw.path.back().name;
                        if (inputs.count(name) == 0) {
                            sta::SchemaField schema;
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
            if (auto * ft = std::get_if<sta::FlowTarget>(&fentry)) {
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
                if (auto * ic = std::get_if<sta::InputChannel>(&ch)) {
                    if (!ic->source.empty() && ic->source.back().name == name) found = true;
                } else if (auto * cc = std::get_if<sta::CallChannel>(&ch)) {
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
    std::map<std::string, sta::PromptSTA> const & prompts,
    std::set<std::string> const & reachable,
    std::map<std::string, sta::SchemaField> & outputs
) {
    for (auto const & pname : reachable) {
        auto it = prompts.find(pname);
        if (it == prompts.end()) continue;
        auto const & prompt = it->second;

        // Check for return flows
        for (auto const & [fname, fentry] : prompt.flows) {
            auto * rt = std::get_if<sta::ReturnTarget>(&fentry);
            if (!rt) continue;
            for (auto const & rf : rt->fields) {
                std::string name = rf.alias.empty() ? "_" : rf.alias;
                sta::SchemaField schema;
                schema.type = "text";
                if (!rf.path.empty()) {
                    std::string field_name = rf.path.back().first;
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
                if (std::holds_alternative<std::monostate>(f.format)) continue; // skip records
                sta::SchemaField schema = format_to_schema(f.format);
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
            sta.python_imports[name] = runtime::sta::PythonImport{ps->filename, target};
        }
    }

    for (auto const & [mangled, pmt_ptr] : prompts) {
        auto const & pmt = *pmt_ptr;
        runtime::sta::PromptSTA pstas;
        pstas.name = pmt.mangled_name;
        pstas.desc = pmt.desc;

        // Carry the prompt-scope search params verbatim (IR Value -> STA
        // SearchValue; identical scalar shapes). Per-field/per-state resolution
        // is applied later at instantiation.
        for (auto const & [category, params] : pmt.search) {
            for (auto const & [key, val] : params) {
                std::visit([&](auto const & v) {
                    pstas.search[category][key] = v;
                }, val);
            }
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
                sta::FieldInfo next_field;
                next_field.name = "next";
                next_field.depth = 1;
                next_field.index = static_cast<int>(pstas.fields.size());
                next_field.flat_index = static_cast<int>(pstas.fields.size());
                sta::EnumFormat ef;
                for (auto const & [label, _] : flow_map) {
                    ef.values.push_back(label);
                }
                next_field.format = ef;
                next_field.desc.push_back("the next step");
                pstas.fields.push_back(std::move(next_field));
            }
        }

    SPDLOG_LOGGER_DEBUG(autocog::log(), "  flat: {} fields", pstas.fields.size());

        // Build abstract states
        auto abstracts = build_abstract(pstas.fields);

    SPDLOG_LOGGER_DEBUG(autocog::log(), "  abstract: {} states", abstracts.size());

        // Build concrete states
        build_concrete_rec(abstracts, pstas.fields, pstas.states, 0, {0});

        // Post-process
        post_process(pstas.fields, pstas.states);

        // Build sequence
        {
            std::string cur = "root@";
            while (true) {
                pstas.sequence.push_back(cur);
                auto it = pstas.states.find(cur);
                if (it == pstas.states.end() || it->second.successors.empty()) break;
                cur = it->second.successors[0];
                if (cur == "root@") break;
            }
        }

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
        sta::EntryPoint ep;
        ep.prompt = mangled;

        auto reachable = reachable_via_flow(sta.prompts, mangled);
        collect_inputs(sta.prompts, reachable, mangled, ep.inputs);
        collect_outputs(sta.prompts, reachable, ep.outputs);

        sta.entry_points[entry_name] = std::move(ep);
    }

    return std::nullopt;
}

} // namespace autocog::compiler::stl
