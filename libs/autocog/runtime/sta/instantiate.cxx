
#include "autocog/runtime/sta/instantiate.hxx"
#include "autocog/utilities/errors.hxx"
#include "autocog/utilities/exception.hxx"

#include <sstream>
#include <set>

namespace autocog::runtime::sta {

using json = nlohmann::json;

// ============================================================================
// Helpers
// ============================================================================

// ============================================================================
// Build concrete states
// ============================================================================

static std::string make_tag(AbstractState const & abs,
                            std::vector<FieldInfo> const & fields,
                            std::vector<int> const & indices) {
    std::string base = abs_tag(abs, fields);
    std::string idx_str;
    for (int i = 1; i < static_cast<int>(indices.size()); ++i) {
        if (i > 1) idx_str += "_";
        idx_str += std::to_string(indices[i]);
    }
    return base + "@" + idx_str;
}

static std::string build_concrete_rec(
    std::vector<AbstractState> const & abstracts,
    std::vector<FieldInfo> const & fields,
    std::map<std::string, ConcreteState> & states,
    int abs_idx,
    std::vector<int> indices
) {
    auto const & abs = abstracts[abs_idx];
    int depth = abs_depth(abs, fields);
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

    ConcreteState cs;
    cs.tag = ctag;
    cs.field = abs.field;
    cs.indices = std::vector<int>(indices.begin() + 1, indices.end());
    states[ctag] = cs;

    if (can_flow) {
        auto new_indices = indices;
        auto const & flow_abs = abstracts[abs.flow];
        if (depth < abs_depth(flow_abs, fields)) {
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
        int delta = depth - abs_depth(exit_abs, fields);
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

static std::vector<std::string> get_sequence(std::map<std::string, ConcreteState> const & states) {
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
    std::vector<FieldInfo> const & fields,
    std::map<std::string, ConcreteState> & states
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

// Build the concrete (index-expanded) state graph from the serialized abstract
// states. This is the work that used to be done in stlc; it now runs here at
// instantiation time. (Currently still max-range expansion — content-driven
// expansion is a later step.) Returns the concrete states; fills `sequence`.
static std::map<std::string, ConcreteState> concretize(
    std::vector<AbstractState> const & abstracts,
    std::vector<FieldInfo> const & fields,
    std::vector<std::string> & sequence
) {
    std::map<std::string, ConcreteState> states;
    if (abstracts.empty()) return states;
    build_concrete_rec(abstracts, fields, states, 0, {0});
    post_process(fields, states);
    std::string cur = "root@";
    while (true) {
        sequence.push_back(cur);
        auto it = states.find(cur);
        if (it == states.end() || it->second.successors.empty()) break;
        cur = it->second.successors[0];
        if (cur == "root@") break;
    }
    return states;
}

static std::vector<int> build_parent_map(std::vector<FieldInfo> const & fields) {
    std::vector<int> parent(fields.size(), -1);
    for (size_t i = 0; i < fields.size(); ++i) {
        if (fields[i].depth <= 1) continue;
        for (int j = static_cast<int>(i) - 1; j >= 0; --j) {
            if (fields[j].depth == fields[i].depth - 1) {
                parent[i] = j;
                break;
            }
        }
    }
    return parent;
}

static std::vector<int> build_ancestors(int field_idx, std::vector<int> const & parent_map) {
    std::vector<int> chain;
    int cur = field_idx;
    while (cur >= 0) {
        chain.push_back(cur);
        cur = parent_map[cur];
    }
    std::reverse(chain.begin(), chain.end());
    return chain;
}

// Validate that content arrays satisfy field range minimums.
// Throws ExecutionError if content provides fewer elements than the field's minimum.
static void validate_content_ranges(
    std::vector<FieldInfo> const & fields,
    std::vector<int> const & parent_map,
    json const & content
) {
    // For each field with a range, navigate content to find the array and check size.
    // Uses a recursive approach to handle nested array-of-record structures.
    struct Walker {
        std::vector<FieldInfo> const & fields;
        std::vector<int> const & parent_map;

        void check(json const & node, int field_idx, std::string const & path) const {
            auto const & fld = fields[field_idx];
            std::string fpath = path.empty() ? fld.name : path + "." + fld.name;

            if (!node.is_object() || !node.contains(fld.name)) return;
            auto const & val = node[fld.name];

            if (fld.range && fld.is_list()) {
                if (val.is_array()) {
                    auto [lo, hi] = *fld.range;
                    int n = static_cast<int>(val.size());
                    if (n < lo) {
                        // A content/range mismatch during instantiation is a
                        // recoverable orchestration failure (the loop may retry
                        // with different tokens), located at the offending field.
                        throw OrchestrationError(
                            "Content provides " + std::to_string(n) +
                            " element(s) for '" + fpath +
                            "' but the field requires at least " + std::to_string(lo),
                            /*prompt=*/"", /*field=*/fpath);
                    }
                    // Check children of each array element
                    for (int i = 0; i < n; ++i) {
                        if (val[i].is_object()) {
                            check_children(val[i], field_idx, fpath + "[" + std::to_string(i) + "]");
                        }
                    }
                }
            } else if (fld.is_record()) {
                if (val.is_object()) {
                    check_children(val, field_idx, fpath);
                }
            }
        }

        void check_children(json const & obj, int parent_idx, std::string const & path) const {
            for (size_t i = 0; i < fields.size(); ++i) {
                if (parent_map[i] == parent_idx) {
                    check(obj, static_cast<int>(i), path);
                }
            }
        }
    };

    Walker w{fields, parent_map};
    // Start with top-level fields (parent == -1)
    for (size_t i = 0; i < fields.size(); ++i) {
        if (parent_map[i] == -1) {
            w.check(content, static_cast<int>(i), "");
        }
    }
}

static json const * read_content(json const & content, ConcreteState const & state,
                                  PromptSTA const & prompt,
                                  std::vector<int> const & parent_map) {
    if (state.field < 0) return nullptr;
    auto ancestors = build_ancestors(state.field, parent_map);
    json const * ptr = &content;

    for (size_t a = 0; a < ancestors.size(); ++a) {
        auto const & f = prompt.fields[ancestors[a]];
        if (ptr->is_object() && ptr->contains(f.name)) {
            ptr = &(*ptr)[f.name];
        } else if (ptr->is_object() || ptr->is_null()) {
            return nullptr;
        }
        int idx = (a < state.indices.size()) ? state.indices[a] : 0;
        if (f.is_list() && ptr->is_array()) {
            if (idx < static_cast<int>(ptr->size())) {
                ptr = &(*ptr)[idx];
            } else {
                return nullptr;
            }
        } else if (f.is_list() && !ptr->is_array()) {
            return nullptr;
        }
    }
    return ptr;
}

static bool should_skip(ConcreteState const & state,
                         PromptSTA const & prompt,
                         json const & content,
                         std::vector<int> const & parent_map) {
    if (state.field < 0) return false;
    auto ancestors = build_ancestors(state.field, parent_map);
    json const * ptr = &content;

    for (size_t a = 0; a < ancestors.size(); ++a) {
        auto const & f = prompt.fields[ancestors[a]];
        if (ptr->is_object() && ptr->contains(f.name)) {
            ptr = &(*ptr)[f.name];
        } else if (ptr->is_object()) {
            return false;
        }
        int idx = (a < state.indices.size()) ? state.indices[a] : 0;
        if (f.is_list() && ptr->is_array()) {
            int content_size = static_cast<int>(ptr->size());
            if (idx >= content_size) {
                return true;
            }
            ptr = &(*ptr)[idx];
        }
    }
    return false;
}

static std::string format_label(FieldInfo const & fld) {
    if (fld.format_ref) return *fld.format_ref;
    return std::visit([](auto const & fmt) -> std::string {
        using F = std::decay_t<decltype(fmt)>;
        if constexpr (std::is_same_v<F, std::monostate>) {
            return "record";
        } else if constexpr (std::is_same_v<F, CompletionFormat>) {
            if (fmt.length) return "text(" + std::to_string(*fmt.length) + ")";
            return "text";
        } else if constexpr (std::is_same_v<F, EnumFormat>) {
            std::string s = "enum(\"";
            for (size_t i = 0; i < fmt.values.size(); ++i) {
                if (i > 0) s += "\",\"";
                s += fmt.values[i];
            }
            s += "\")";
            return s;
        } else if constexpr (std::is_same_v<F, ChoiceFormat>) {
            std::string path;
            for (auto const & step : fmt.path) {
                auto const & name = step.name;
                if (!path.empty()) path += ".";
                path += name;
            }
            return fmt.mode + "(" + path + ")";
        }
        return "?";
    }, fld.format);
}

static std::string range_str(FieldInfo const & fld) {
    if (!fld.range) return "";
    auto [lo, hi] = *fld.range;
    if (lo == hi) return "[" + std::to_string(lo) + "]";
    return "[" + std::to_string(lo) + ":" + std::to_string(hi) + "]";
}

static std::string format_str(FieldInfo const & fld) {
    // Full format description (for named format listing)
    return std::visit([](auto const & fmt) -> std::string {
        using F = std::decay_t<decltype(fmt)>;
        if constexpr (std::is_same_v<F, std::monostate>) {
            return "record";
        } else if constexpr (std::is_same_v<F, CompletionFormat>) {
            if (fmt.length) return "text(" + std::to_string(*fmt.length) + ")";
            return "text";
        } else if constexpr (std::is_same_v<F, EnumFormat>) {
            std::string s = "enum(\"";
            for (size_t i = 0; i < fmt.values.size(); ++i) {
                if (i > 0) s += "\",\"";
                s += fmt.values[i];
            }
            s += "\")";
            return s;
        } else if constexpr (std::is_same_v<F, ChoiceFormat>) {
            std::string path;
            for (auto const & step : fmt.path) {
                auto const & name = step.name;
                if (!path.empty()) path += ".";
                path += name;
            }
            return fmt.mode + "(" + path + ")";
        }
        return "?";
    }, fld.format);
}

static std::string prompt_label(ConcreteState const & state,
                                 PromptSTA const & prompt,
                                 Syntax const & syntax) {
    if (state.field < 0) return "";
    auto const & fld = prompt.fields[state.field];
    std::string indent;
    for (int i = 1; i < fld.depth; ++i) indent += syntax.prompt_indent;
    std::string label = indent + fld.name;
    if (syntax.prompt_with_format) {
        label += "(" + format_label(fld) + ")";
    }
    if (fld.is_list() && !state.indices.empty()) {
        int idx = state.indices.back();
        if (!syntax.prompt_zero_index) idx += 1;
        label += "[" + std::to_string(idx) + "]";
    }
    label += syntax.field_suffix;
    return label;
}

static std::vector<std::string> ravel_choices(
    json const & content,
    ChoiceFormat const & cf,
    Syntax const & syntax
) {
    std::vector<json const *> current = {&content};
    for (auto const & step : cf.path) {
        auto const & name = step.name;
        std::vector<json const *> next;
        for (auto const * ptr : current) {
            if (ptr->is_object() && ptr->contains(name)) {
                auto const & child = (*ptr)[name];
                if (child.is_array()) {
                    for (auto const & elem : child) next.push_back(&elem);
                } else {
                    next.push_back(&child);
                }
            } else if (ptr->is_array()) {
                for (auto const & elem : *ptr) {
                    if (elem.is_object() && elem.contains(name))
                        next.push_back(&elem[name]);
                }
            }
        }
        current = std::move(next);
    }

    std::vector<std::string> choices;
    if (cf.mode == "select") {
        int offset = syntax.prompt_zero_index ? 0 : 1;
        for (int i = 0; i < static_cast<int>(current.size()); ++i)
            choices.push_back(std::to_string(i + offset));
    } else {
        for (auto const * ptr : current)
            choices.push_back(ptr->is_string() ? ptr->get<std::string>() : ptr->dump());
    }
    return choices;
}

// ============================================================================
// FTA builder state
// ============================================================================

struct FTABuilder {
    json actions = json::array();
    int action_id = 0;

    PromptSTA const & prompt;
    std::map<std::string, ConcreteState> const & states;  // built by concretize()
    json const & content;
    Syntax const & syntax;
    SearchConfig const & search;
    std::vector<int> parent_map;
    std::set<int> described_fields;
    std::map<std::string, int> memo;  // state_tag → entry action ID

    // Create field+endl for a successor, with UIDs scoped to the parent.
    // Returns the field entry ID (or -1 for records).
    // Connects field → endl. Does NOT connect branch → field (caller does that).
    std::pair<int, int> create_field_endl(
        std::string const & parent_safe,
        std::string const & succ_tag
    ) {
        auto const & succ = states.at(succ_tag);
        std::string succ_safe = safe(succ_tag);
        std::string uid_prefix = succ_safe + ".from." + parent_safe;

        auto [field_id, choose_count] = create_field(succ, uid_prefix);
        int prev = field_id;

        if (field_id < 0) {
            // Record node: create empty connector
            prev = add_text("skip." + uid_prefix, "");
        }

        int endl_id = add_text("endl." + uid_prefix, syntax.field_separator);
        if (choose_count > 0) {
            connect_choose(prev, endl_id, choose_count);
        } else {
            connect(prev, endl_id);
        }

        return {prev == field_id ? field_id : prev, endl_id};
    }

    FTABuilder(PromptSTA const & p, std::map<std::string, ConcreteState> const & st,
               json const & c, Syntax const & s, SearchConfig const & sc)
        : prompt(p), states(st), content(c), syntax(s), search(sc), parent_map(build_parent_map(p.fields)) {}

    int add_text(std::string const & uid, std::string const & text) {
        int id = action_id++;
        actions.push_back({
            {"uid", uid}, {"type", "text"}, {"text", text}, {"successors", json::array()}
        });
        return id;
    }

    // Resolve a per-field policy against the config defaults into the typed
    // structs xfta consumes. The policy (open category->param map from the IR/
    // STA) wins per-param; otherwise the config default is used. The full
    // context (field policy, prompt/STA, state, indices) is available for future
    // advanced policies; the raw policy below is just policy-value ?? config.
    static std::optional<float> pol_f(SearchParams const & pol, std::string const & cat, char const * key) {
        auto c = pol.find(cat);
        if (c == pol.end()) return std::nullopt;
        auto p = c->second.find(key);
        if (p == c->second.end()) return std::nullopt;
        if (auto const * v = std::get_if<float>(&p->second)) return *v;
        if (auto const * iv = std::get_if<int>(&p->second)) return static_cast<float>(*iv);
        return std::nullopt;
    }
    static std::optional<unsigned> pol_u(SearchParams const & pol, std::string const & cat, char const * key) {
        auto c = pol.find(cat);
        if (c == pol.end()) return std::nullopt;
        auto p = c->second.find(key);
        if (p == c->second.end()) return std::nullopt;
        if (auto const * iv = std::get_if<int>(&p->second)) return static_cast<unsigned>(*iv);
        return std::nullopt;
    }

    TextSearch resolve_text(SearchParams const & pol) const {
        TextSearch r = search.text;  // config defaults
        if (auto v = pol_f(pol, "text", "threshold")) r.threshold = *v;
        if (auto v = pol_u(pol, "text", "beams"))     r.beams = *v;
        if (auto v = pol_u(pol, "text", "ahead"))     r.ahead = *v;
        if (auto v = pol_u(pol, "text", "width"))     r.width = *v;
        if (auto v = pol_f(pol, "text", "repetition")) r.repetition = *v;
        if (auto v = pol_f(pol, "text", "diversity"))  r.diversity = *v;
        return r;
    }
    ChoiceSearch resolve_choice(SearchParams const & pol, std::string const & cat,
                                ChoiceSearch const & defaults) const {
        ChoiceSearch r = defaults;
        if (auto v = pol_f(pol, cat, "threshold")) r.threshold = *v;
        if (auto v = pol_u(pol, cat, "width"))     r.width = *v;
        return r;
    }

    int add_choose(std::string const & uid, std::vector<std::string> const & choices,
                   ChoiceSearch const & cs) {
        int id = action_id++;
        actions.push_back({
            {"uid", uid}, {"type", "choose"}, {"choices", choices},
            {"threshold", cs.threshold},
            {"width", cs.width},
            {"successors", json::array()}
        });
        return id;
    }

    int add_complete(std::string const & uid, CompletionFormat const & cf, TextSearch const & ts) {
        int id = action_id++;
        json j = {
            {"uid", uid}, {"type", "complete"},
            {"length", cf.length.has_value() ? cf.length.value() : 50},
            {"threshold", ts.threshold},
            {"beams", ts.beams},
            {"ahead", ts.ahead},
            {"width", ts.width},
            {"stop_text", syntax.completion_stop},
            {"successors", json::array()}
        };
        if (ts.repetition) j["repetition"] = *ts.repetition;
        if (ts.diversity) j["diversity"] = *ts.diversity;
        if (cf.vocab) j["vocab"] = *cf.vocab;
        actions.push_back(j);
        return id;
    }

    void connect(int from, int to) {
        actions[from]["successors"].push_back(actions[to]["uid"]);
    }

    void connect_choose(int choose_id, int next_id, int num_choices) {
        for (int i = 0; i < num_choices; ++i)
            actions[choose_id]["successors"].push_back(actions[next_id]["uid"]);
    }

    static std::string safe(std::string tag) {
        for (auto & c : tag) if (c == '@') c = '_';
        return tag;
    }

    // Create field content node for a successor state.
    // Returns (field_id, choose_count) — choose_count > 0 for enum/choice fields.
    std::pair<int, int> create_field(ConcreteState const & succ, std::string const & succ_safe) {
        auto const & fld = prompt.fields[succ.field];
        if (fld.is_record()) return {-1, 0};

        auto const * val = read_content(content, succ, prompt, parent_map);
        int field_id = -1;
        int choose_count = 0;

        // Metadata for psta: field index and indices enable direct field lookup
        auto add_field_meta = [&](int id) {
            actions[id]["field"] = succ.field;
            json idx = json::array();
            for (auto i : succ.indices) idx.push_back(i);
            actions[id]["indices"] = idx;
        };

        std::visit([&](auto const & fmt) {
            using F = std::decay_t<decltype(fmt)>;
            // The field's resolved policy (from the IR cascade, carried on the STA).
            SearchParams const & pol = fld.search;
            if constexpr (std::is_same_v<F, std::monostate>) {
                // record — skip
            } else if constexpr (std::is_same_v<F, CompletionFormat>) {
                if (val && !val->is_null()) {
                    std::string text = val->is_string() ? val->get<std::string>() : val->dump();
                    field_id = add_text("field." + succ_safe, text);
                } else {
                    field_id = add_complete("field." + succ_safe, fmt, resolve_text(pol));
                }
                add_field_meta(field_id);
            } else if constexpr (std::is_same_v<F, EnumFormat>) {
                if (val && !val->is_null()) {
                    std::string text = val->is_string() ? val->get<std::string>() : val->dump();
                    field_id = add_text("field." + succ_safe, text);
                } else {
                    // The synthetic "next" field is the flow choice; real enums
                    // are enum-content choices.
                    bool is_flow = (fld.name == "next");
                    ChoiceSearch cs = is_flow
                        ? resolve_choice(pol, "flow", search.flow)
                        : resolve_choice(pol, "enum", search.enums);
                    field_id = add_choose("field." + succ_safe, fmt.values, cs);
                    choose_count = static_cast<int>(fmt.values.size());
                }
                add_field_meta(field_id);
            } else if constexpr (std::is_same_v<F, ChoiceFormat>) {
                if (val && !val->is_null()) {
                    std::string text = val->is_string() ? val->get<std::string>() : val->dump();
                    field_id = add_text("field." + succ_safe, text);
                } else {
                    auto choices = ravel_choices(content, fmt, syntax);
                    field_id = add_choose("field." + succ_safe, choices,
                                          resolve_choice(pol, "enum", search.enums));
                    choose_count = static_cast<int>(choices.size());
                }
                add_field_meta(field_id);
            }
        }, fld.format);

        return {field_id, choose_count};
    }

    // Recursive FTA instantiation following the STA state graph.
    // Returns the branch action ID, or -1 if no successors.
    int instantiate_rec(std::string const & state_tag) {
        // Memoize: if already processed, return the same entry node
        auto memo_it = memo.find(state_tag);
        if (memo_it != memo.end()) return memo_it->second;

        auto it = states.find(state_tag);
        if (it == states.end()) { memo[state_tag] = -1; return -1; }
        auto const & state = it->second;

        // Filter valid successors
        std::vector<std::string> valid;
        for (auto const & succ_tag : state.successors) {
            auto sit = states.find(succ_tag);
            if (sit == states.end()) continue;
            if (!should_skip(sit->second, prompt, content, parent_map))
                valid.push_back(succ_tag);
        }
        if (valid.empty()) { memo[state_tag] = -1; return -1; }

        std::string cur_safe = safe(state_tag);
        int entry_id;

        if (valid.size() == 1) {
            // Single successor: desc (optional) → branch (Text) → field → endl → recurse
            auto const & succ = states.at(valid[0]);
            auto const & fld = prompt.fields[succ.field];
            std::string succ_safe = safe(valid[0]);

            // Description before label
            int desc_id = -1;
            if (syntax.desc_inline && !fld.desc.empty() && described_fields.find(succ.field) == described_fields.end()) {
                described_fields.insert(succ.field);
                std::string desc_text;
                for (auto const & d : fld.desc)
                    desc_text += syntax.desc_pre + d + syntax.desc_post;
                desc_id = add_text("desc." + succ_safe, desc_text);
            }

            // Branch (label)
            int branch_id = add_text("branch." + cur_safe, prompt_label(succ, prompt, syntax));

            if (desc_id >= 0) {
                connect(desc_id, branch_id);
                entry_id = desc_id;
            } else {
                entry_id = branch_id;
            }

            // Field + endl
            auto [field_entry, endl_id] = create_field_endl(cur_safe, valid[0]);
            connect(branch_id, field_entry);

            // Recurse
            int next = instantiate_rec(valid[0]);
            if (next >= 0) connect(endl_id, next);

        } else {
            // Multiple successors: check if content determines the choice.
            // When content pre-fills a successor's value, that path must be taken
            // (content determines array length, not the model).
            int content_forced = -1;
            for (size_t i = 0; i < valid.size(); ++i) {
                auto const & succ = states.at(valid[i]);
                auto const * val = read_content(content, succ, prompt, parent_map);
                if (val && !val->is_null()) {
                    content_forced = static_cast<int>(i);
                    break;  // first pre-filled successor wins
                }
            }

            if (content_forced >= 0) {
                // Content determines the path: use text node (forced label)
                auto const & succ = states.at(valid[content_forced]);
                int branch_id = add_text("branch." + cur_safe,
                                         prompt_label(succ, prompt, syntax));
                entry_id = branch_id;

                auto [field_entry, endl_id] = create_field_endl(cur_safe, valid[content_forced]);
                connect(branch_id, field_entry);

                int next = instantiate_rec(valid[content_forced]);
                if (next >= 0) connect(endl_id, next);
            } else {
                // No content determines the choice: model decides (Choose node)
                std::vector<std::string> choices;
                for (auto const & succ_tag : valid) {
                    auto const & succ = states.at(succ_tag);
                    choices.push_back(prompt_label(succ, prompt, syntax));
                }
                SearchParams empty_pol;
                SearchParams const & branch_pol =
                    (state.field >= 0) ? prompt.fields[state.field].search : empty_pol;
                int branch_id = add_choose("branch." + cur_safe, choices,
                    resolve_choice(branch_pol, "branch", search.branch));
                entry_id = branch_id;

                for (size_t i = 0; i < valid.size(); ++i) {
                    auto [field_entry, endl_id] = create_field_endl(cur_safe, valid[i]);
                    actions[branch_id]["successors"].push_back(actions[field_entry]["uid"]);

                    int next = instantiate_rec(valid[i]);
                    if (next >= 0) connect(endl_id, next);
                }
            }
        }

        memo[state_tag] = entry_id;
        return entry_id;
    }
};

// ============================================================================
// Instantiate
// ============================================================================

json instantiate(PromptSTA const & prompt, json const & content,
                 Syntax const & syntax, SearchConfig const & search) {
    auto parent_map = build_parent_map(prompt.fields);
    validate_content_ranges(prompt.fields, parent_map, content);

    // Concretize the abstract state graph here (was previously done in stlc).
    std::vector<std::string> sequence;
    auto states = concretize(prompt.abstracts, prompt.fields, sequence);

    FTABuilder b(prompt, states, content, syntax, search);

    // Build header
    std::ostringstream hdr;
    hdr << syntax.header_pre << syntax.system_msg << syntax.header_mid;

    // Prompt description
    for (auto const & d : prompt.desc) hdr << d << " ";
    hdr << "\n";

    // Mechanics: schema preview in a code block
    hdr << syntax.header_mechanic << "\n```\n";
    hdr << "start:\n";
    for (auto const & fld : prompt.fields) {
        std::string indent;
        for (int i = 1; i < fld.depth; ++i) indent += syntax.prompt_indent;
        hdr << indent << fld.name;
        hdr << "(" << format_label(fld) << ")";
        hdr << range_str(fld);
        hdr << ":";
        if (!fld.desc.empty()) {
            for (auto const & d : fld.desc) hdr << " " << d;
        }
        hdr << "\n";
    }
    hdr << "```";

    // Named format descriptions
    std::set<std::string> seen_refs;
    std::vector<std::pair<FieldInfo const *, std::string>> named_formats;
    for (auto const & fld : prompt.fields) {
        if (fld.format_ref && seen_refs.find(*fld.format_ref) == seen_refs.end()) {
            seen_refs.insert(*fld.format_ref);
            named_formats.push_back({&fld, *fld.format_ref});
        }
    }
    if (!named_formats.empty()) {
        hdr << "\n" << syntax.header_formats << "\n";
        for (auto const & [fld_ptr, ref_name] : named_formats) {
            hdr << syntax.format_listing << ref_name << ": " << format_str(*fld_ptr) << "\n";
            for (auto const & d : fld_ptr->format_desc) {
                hdr << "  " << syntax.format_listing << d << "\n";
            }
        }
    }

    hdr << syntax.header_post << "start:" << syntax.field_separator;
    int header_id = b.add_text("header", hdr.str());

    // Recursive instantiation from root
    // The "next" field (flow control) is now a regular field in the state graph,
    // so it's instantiated by the recursive walk like any other field.
    int first_branch = b.instantiate_rec("root@");
    if (first_branch >= 0) {
        b.connect(header_id, first_branch);
    }

    // Queue params (prompt-scope): carried into the FTA even though the current
    // xfta queue does not consume them yet. Policy (prompt.search["queue"]) wins
    // over the config default. TODO(xfta-queue).
    std::string metric = search.queue.metric;
    auto qit = prompt.search.find("queue");
    if (qit != prompt.search.end()) {
        auto mit = qit->second.find("metric");
        if (mit != qit->second.end())
            if (auto const * s = std::get_if<std::string>(&mit->second)) metric = *s;
    }

    json fta = json{{"actions", b.actions}, {"queue", json{{"metric", metric}}}};
    // Vocab table: carried into the FTA so the backend (xfta) can build token
    // masks from each vocab_<hash> -> STL expression. complete actions reference
    // an entry by its key.
    if (!prompt.vocabs.empty()) {
        json vj = json::object();
        for (auto const & [k, ve] : prompt.vocabs) vj[k] = ::autocog::runtime::fta::vocab_to_json(ve);
        fta["vocabs"] = vj;
    }
    return fta;
}

// ============================================================================
// Render text
// ============================================================================

std::string render_text(json const & fta) {
    auto const & actions = fta["actions"];
    if (actions.empty()) return "";

    std::map<std::string, size_t> uid_to_idx;
    for (size_t i = 0; i < actions.size(); ++i)
        uid_to_idx[actions[i]["uid"].get<std::string>()] = i;

    std::ostringstream out;
    size_t current = 0;
    std::set<size_t> visited;
    while (visited.find(current) == visited.end()) {
        visited.insert(current);
        auto const & action = actions[current];

        if (action["type"] == "text") {
            out << action["text"].get<std::string>();
        } else if (action["type"] == "choose") {
            auto const & choices = action["choices"];
            out << "[";
            for (size_t i = 0; i < choices.size(); ++i) {
                if (i > 0) out << "|";
                out << choices[i].get<std::string>();
            }
            out << "]";
        } else if (action["type"] == "complete") {
            int len = action.value("length", 20);
            out << "[..." << len << " tokens]";
        }

        auto const & succs = action["successors"];
        if (succs.empty()) break;
        auto next_uid = succs[0].get<std::string>();
        auto it = uid_to_idx.find(next_uid);
        if (it == uid_to_idx.end()) break;
        current = it->second;
    }
    return out.str();
}

}
