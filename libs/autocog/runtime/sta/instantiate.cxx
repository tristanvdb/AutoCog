
#include "autocog/runtime/sta/instantiate.hxx"

#include <sstream>
#include <set>

namespace autocog::runtime::sta {

using json = nlohmann::json;

// ============================================================================
// Helpers
// ============================================================================

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
            for (auto const & [name, _] : fmt.path) {
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
            for (auto const & [name, _] : fmt.path) {
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
    for (auto const & [name, range] : cf.path) {
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
    json const & content;
    Syntax const & syntax;
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
        auto const & succ = prompt.states.at(succ_tag);
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

    FTABuilder(PromptSTA const & p, json const & c, Syntax const & s)
        : prompt(p), content(c), syntax(s), parent_map(build_parent_map(p.fields)) {}

    int add_text(std::string const & uid, std::string const & text) {
        int id = action_id++;
        actions.push_back({
            {"uid", uid}, {"type", "text"}, {"text", text}, {"successors", json::array()}
        });
        return id;
    }

    int add_choose(std::string const & uid, std::vector<std::string> const & choices) {
        int id = action_id++;
        actions.push_back({
            {"uid", uid}, {"type", "choose"}, {"choices", choices}, {"successors", json::array()}
        });
        return id;
    }

    int add_complete(std::string const & uid, CompletionFormat const & cf) {
        int id = action_id++;
        json j = {
            {"uid", uid}, {"type", "complete"}, {"successors", json::array()}
        };
        if (cf.length.has_value()) j["length"] = cf.length.value();
        if (cf.threshold.has_value()) j["threshold"] = cf.threshold.value();
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

        std::visit([&](auto const & fmt) {
            using F = std::decay_t<decltype(fmt)>;
            if constexpr (std::is_same_v<F, std::monostate>) {
                // record — skip
            } else if constexpr (std::is_same_v<F, CompletionFormat>) {
                if (val && !val->is_null()) {
                    std::string text = val->is_string() ? val->get<std::string>() : val->dump();
                    field_id = add_text("field." + succ_safe, text);
                } else {
                    field_id = add_complete("field." + succ_safe, fmt);
                }
            } else if constexpr (std::is_same_v<F, EnumFormat>) {
                if (val && !val->is_null()) {
                    std::string text = val->is_string() ? val->get<std::string>() : val->dump();
                    field_id = add_text("field." + succ_safe, text);
                } else {
                    field_id = add_choose("field." + succ_safe, fmt.values);
                    choose_count = static_cast<int>(fmt.values.size());
                }
            } else if constexpr (std::is_same_v<F, ChoiceFormat>) {
                if (val && !val->is_null()) {
                    std::string text = val->is_string() ? val->get<std::string>() : val->dump();
                    field_id = add_text("field." + succ_safe, text);
                } else {
                    auto choices = ravel_choices(content, fmt, syntax);
                    field_id = add_choose("field." + succ_safe, choices);
                    choose_count = static_cast<int>(choices.size());
                }
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

        auto it = prompt.states.find(state_tag);
        if (it == prompt.states.end()) { memo[state_tag] = -1; return -1; }
        auto const & state = it->second;

        // Filter valid successors
        std::vector<std::string> valid;
        for (auto const & succ_tag : state.successors) {
            auto sit = prompt.states.find(succ_tag);
            if (sit == prompt.states.end()) continue;
            if (!should_skip(sit->second, prompt, content, parent_map))
                valid.push_back(succ_tag);
        }
        if (valid.empty()) { memo[state_tag] = -1; return -1; }

        std::string cur_safe = safe(state_tag);
        int entry_id;

        if (valid.size() == 1) {
            // Single successor: desc (optional) → branch (Text) → field → endl → recurse
            auto const & succ = prompt.states.at(valid[0]);
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
            // Multiple successors: Choose node with field labels as choices
            std::vector<std::string> choices;
            for (auto const & succ_tag : valid) {
                auto const & succ = prompt.states.at(succ_tag);
                choices.push_back(prompt_label(succ, prompt, syntax));
            }
            int branch_id = add_choose("branch." + cur_safe, choices);
            entry_id = branch_id;

            for (size_t i = 0; i < valid.size(); ++i) {
                // Field + endl (unique UIDs per parent)
                auto [field_entry, endl_id] = create_field_endl(cur_safe, valid[i]);
                actions[branch_id]["successors"].push_back(actions[field_entry]["uid"]);

                // Recurse (memoized — shared subtree)
                int next = instantiate_rec(valid[i]);
                if (next >= 0) connect(endl_id, next);
            }
        }

        memo[state_tag] = entry_id;
        return entry_id;
    }
};

// ============================================================================
// Instantiate
// ============================================================================

json instantiate(PromptSTA const & prompt, json const & content, Syntax const & syntax) {
    FTABuilder b(prompt, content, syntax);

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
    if (!prompt.flows.empty()) {
        hdr << "next: select which of ";
        bool first = true;
        for (auto const & [name, _] : prompt.flows) {
            if (!first) hdr << ",";
            hdr << name;
            first = false;
        }
        hdr << " will be the next step.\n";
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
    int first_branch = b.instantiate_rec("root@");
    if (first_branch >= 0) {
        b.connect(header_id, first_branch);
    }

    // Find terminal states (no successors) and connect to flow control
    // Terminal states are those where instantiate_rec returned -1,
    // i.e., the last endl nodes with no further branch connected.
    // We find them by looking for endl nodes with empty successors.
    int next_field = b.add_text("next.field", "next: ");

    for (size_t i = 0; i < b.actions.size(); ++i) {
        auto const & a = b.actions[i];
        std::string uid = a["uid"].get<std::string>();
        if (uid.substr(0, 5) == "endl." && a["successors"].empty()) {
            b.connect(static_cast<int>(i), next_field);
        }
    }

    // Flow control
    std::vector<std::string> flow_choices;
    for (auto const & [name, _] : prompt.flows) {
        flow_choices.push_back(name);
    }
    if (flow_choices.size() == 1) {
        int next_id = b.add_text("next.choice", flow_choices[0]);
        b.connect(next_field, next_id);
    } else if (!flow_choices.empty()) {
        int choose_id = b.add_choose("next.choose", flow_choices);
        b.connect(next_field, choose_id);
        for (size_t i = 0; i < flow_choices.size(); ++i) {
            int end_id = b.add_text("next.end." + std::to_string(i), "");
            b.connect(choose_id, end_id);
        }
    }

    return json{{"actions", b.actions}};
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
