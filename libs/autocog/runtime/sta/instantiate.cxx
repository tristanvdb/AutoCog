
#include "autocog/runtime/sta/instantiate.hxx"

#include <sstream>

namespace autocog::runtime::sta {

using json = nlohmann::json;

// ============================================================================
// Helpers
// ============================================================================

// Precompute parent field indices for nested navigation
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

// Build ancestor chain (from root to this field)
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

// Navigate content dict following the nested path to find the value
// for a specific field at specific indices.
// Handles auto-flattening: when content provides a scalar but the STA
// expects a single-field record, treat the scalar as the inner field's value.
static json const * read_content(json const & content, ConcreteState const & state,
                                  PromptSTA const & prompt,
                                  std::vector<int> const & parent_map) {
    if (state.field < 0) return nullptr;

    auto ancestors = build_ancestors(state.field, parent_map);
    json const * ptr = &content;

    for (size_t a = 0; a < ancestors.size(); ++a) {
        auto const & f = prompt.fields[ancestors[a]];

        // Try to navigate into the field by name
        if (ptr->is_object() && ptr->contains(f.name)) {
            ptr = &(*ptr)[f.name];
        } else if (ptr->is_object() || ptr->is_null()) {
            return nullptr;
        }
        // else: ptr is a scalar or array — auto-flatten
        // (content provides a scalar where STA expects a record wrapper)

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

        // If not the last step and ptr is a scalar, we're auto-flattening:
        // the content skipped the record wrapper, so we just keep ptr as-is
        // and let the next iteration try to navigate further (or use it)
    }
    return ptr;
}

// Check if a state should be skipped because its parent array index
// exceeds the content array size.
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
            return false; // no content for this path → don't skip, generate
        }
        // else: scalar → auto-flattening, continue

        int idx = (a < state.indices.size()) ? state.indices[a] : 0;

        if (f.is_list() && ptr->is_array()) {
            int content_size = static_cast<int>(ptr->size());
            if (idx >= content_size) {
                return true; // index beyond content → skip
            }
            ptr = &(*ptr)[idx];
        }
    }
    return false;
}

static std::string prompt_label(ConcreteState const & state,
                                 PromptSTA const & prompt,
                                 Syntax const & syntax) {
    if (state.field < 0) return "";
    auto const & fld = prompt.fields[state.field];
    std::string indent;
    for (int i = 1; i < fld.depth; ++i) indent += syntax.prompt_indent;
    std::string label = indent + fld.name;
    if (fld.is_list() && !state.indices.empty()) {
        label += "[" + std::to_string(state.indices.back()) + "]";
    }
    label += syntax.field_suffix;
    return label;
}

static std::vector<std::string> ravel_choices(
    json const & content,
    ChoiceFormat const & cf,
    Syntax const & syntax
) {
    // Collect values by navigating the path, raveling through arrays
    std::vector<json const *> current = {&content};

    for (auto const & [name, range] : cf.path) {
        std::vector<json const *> next;
        for (auto const * ptr : current) {
            if (ptr->is_object() && ptr->contains(name)) {
                auto const & child = (*ptr)[name];
                if (child.is_array()) {
                    for (auto const & elem : child) {
                        next.push_back(&elem);
                    }
                } else {
                    next.push_back(&child);
                }
            } else if (ptr->is_array()) {
                // Map over array elements
                for (auto const & elem : *ptr) {
                    if (elem.is_object() && elem.contains(name)) {
                        next.push_back(&elem[name]);
                    }
                }
            }
        }
        current = std::move(next);
    }

    std::vector<std::string> choices;
    if (cf.mode == "select") {
        int offset = syntax.prompt_zero_index ? 0 : 1;
        for (int i = 0; i < static_cast<int>(current.size()); ++i) {
            choices.push_back(std::to_string(i + offset));
        }
    } else {
        for (auto const * ptr : current) {
            choices.push_back(ptr->is_string() ? ptr->get<std::string>() : ptr->dump());
        }
    }
    return choices;
}

// ============================================================================
// Instantiate
// ============================================================================

json instantiate(PromptSTA const & prompt, json const & content, Syntax const & syntax) {
    json actions = json::array();
    int action_id = 0;

    auto parent_map = build_parent_map(prompt.fields);

    auto add_text = [&](std::string const & uid, std::string const & text) -> int {
        int id = action_id++;
        actions.push_back({
            {"uid", uid}, {"type", "text"}, {"text", text}, {"successors", json::array()}
        });
        return id;
    };

    auto add_choose = [&](std::string const & uid, std::vector<std::string> const & choices) -> int {
        int id = action_id++;
        actions.push_back({
            {"uid", uid}, {"type", "choose"}, {"choices", choices}, {"successors", json::array()}
        });
        return id;
    };

    auto add_complete = [&](std::string const & uid, CompletionFormat const & cf) -> int {
        int id = action_id++;
        json j = {
            {"uid", uid}, {"type", "complete"}, {"successors", json::array()}
        };
        if (cf.length) j["length"] = *cf.length;
        if (cf.threshold) j["threshold"] = *cf.threshold;
        if (cf.beams) j["beams"] = *cf.beams;
        if (cf.ahead) j["ahead"] = *cf.ahead;
        if (cf.width) j["width"] = *cf.width;
        actions.push_back(std::move(j));
        return id;
    };

    auto connect = [&](int from, int to) {
        actions[from]["successors"].push_back(actions[to]["uid"]);
    };

    auto connect_choose = [&](int choose_id, int next_id, int num_choices) {
        for (int i = 0; i < num_choices; ++i) {
            actions[choose_id]["successors"].push_back(actions[next_id]["uid"]);
        }
    };

    // Header
    std::string header = syntax.header_pre + syntax.system_msg + syntax.header_mid;
    for (auto const & d : prompt.desc) header += d + " ";
    header += syntax.header_post + "start:" + syntax.field_separator;
    int header_id = add_text("header", header);

    // Walk sequence
    int prev_end = header_id;
    for (size_t s = 1; s < prompt.sequence.size(); ++s) {
        auto const & tag = prompt.sequence[s];
        auto it = prompt.states.find(tag);
        if (it == prompt.states.end()) continue;
        auto const & state = it->second;
        if (state.field < 0) continue;

        auto const & fld = prompt.fields[state.field];
        std::string safe_tag = tag;
        for (auto & c : safe_tag) if (c == '@') c = '_';

        // Skip states beyond content array bounds
        if (should_skip(state, prompt, content, parent_map)) {
            continue;
        }

        std::string label = prompt_label(state, prompt, syntax);
        int branch_id = add_text("branch." + safe_tag, label);
        connect(prev_end, branch_id);

        int field_prev = branch_id;
        int choose_count = 0;

        if (!fld.is_record()) {
            auto const * val = read_content(content, state, prompt, parent_map);

            std::visit([&](auto const & fmt) {
                using F = std::decay_t<decltype(fmt)>;
                if constexpr (std::is_same_v<F, std::monostate>) {
                    // record — skip
                } else if constexpr (std::is_same_v<F, CompletionFormat>) {
                    if (val && !val->is_null()) {
                        std::string text = val->is_string() ? val->get<std::string>() : val->dump();
                        int id = add_text("field." + safe_tag, text);
                        connect(branch_id, id);
                        field_prev = id;
                    } else {
                        int id = add_complete("field." + safe_tag, fmt);
                        connect(branch_id, id);
                        field_prev = id;
                    }
                } else if constexpr (std::is_same_v<F, EnumFormat>) {
                    if (val && !val->is_null()) {
                        std::string text = val->is_string() ? val->get<std::string>() : val->dump();
                        int id = add_text("field." + safe_tag, text);
                        connect(branch_id, id);
                        field_prev = id;
                    } else {
                        int id = add_choose("field." + safe_tag, fmt.values);
                        connect(branch_id, id);
                        field_prev = id;
                        choose_count = static_cast<int>(fmt.values.size());
                    }
                } else if constexpr (std::is_same_v<F, ChoiceFormat>) {
                    if (val && !val->is_null()) {
                        std::string text = val->is_string() ? val->get<std::string>() : val->dump();
                        int id = add_text("field." + safe_tag, text);
                        connect(branch_id, id);
                        field_prev = id;
                    } else {
                        auto choices = ravel_choices(content, fmt, syntax);
                        int id = add_choose("field." + safe_tag, choices);
                        connect(branch_id, id);
                        field_prev = id;
                        choose_count = static_cast<int>(choices.size());
                    }
                }
            }, fld.format);
        }

        int endl_id = add_text("endl." + safe_tag, syntax.field_separator);
        if (choose_count > 0) {
            connect_choose(field_prev, endl_id, choose_count);
        } else {
            connect(field_prev, endl_id);
        }
        prev_end = endl_id;
    }

    // Flow control
    int next_field = add_text("next.field", "next: ");
    connect(prev_end, next_field);

    std::vector<std::string> flow_choices;
    for (auto const & [name, _] : prompt.flows) {
        flow_choices.push_back(name);
    }
    if (flow_choices.size() == 1) {
        int next_id = add_text("next.choice", flow_choices[0]);
        connect(next_field, next_id);
    } else if (!flow_choices.empty()) {
        int choose_id = add_choose("next.choose", flow_choices);
        connect(next_field, choose_id);
        for (size_t i = 0; i < flow_choices.size(); ++i) {
            int end_id = add_text("next.end." + std::to_string(i), "");
            connect(choose_id, end_id);
        }
    }

    return json{{"actions", actions}};
}

// ============================================================================
// Render text
// ============================================================================

std::string render_text(json const & fta) {
    std::ostringstream out;
    for (auto const & action : fta["actions"]) {
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
    }
    return out.str();
}

}
