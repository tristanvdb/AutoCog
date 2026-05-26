
#include "autocog/runtime/sta/parse.hxx"

// GCC 13 false positive: std::variant operations in set_value trigger
// maybe-uninitialized warnings with -O2. This is a known GCC issue.
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmaybe-uninitialized"
#endif

#include <sstream>
#include <stdexcept>
#include <algorithm>

namespace autocog::runtime::sta {

using json = nlohmann::json;

// ============================================================================
// Parse text → FieldRecord
// ============================================================================

// Build the expected label for a field+state, same as instantiate's prompt_label
static std::string format_label_str(FieldInfo const & fld) {
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

static std::string expected_label(ConcreteState const & state,
                                   PromptSTA const & prompt,
                                   Syntax const & syntax) {
    if (state.field < 0) return "";
    auto const & fld = prompt.fields[state.field];
    std::string indent;
    for (int i = 1; i < fld.depth; ++i) indent += syntax.prompt_indent;
    std::string label = indent + fld.name;
    if (syntax.prompt_with_format) {
        label += "(" + format_label_str(fld) + ")";
    }
    if (fld.is_list() && !state.indices.empty()) {
        int idx = state.indices.back();
        if (!syntax.prompt_zero_index) idx += 1;
        label += "[" + std::to_string(idx) + "]";
    }
    label += syntax.field_suffix;
    return label;
}

// Precompute parent field indices: for each field at depth d,
// find the nearest preceding field at depth d-1.
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

// Navigate to the correct position in the nested record structure,
// creating intermediate arrays and records as needed.
// Then set the value at the final position.
static void set_value(FieldRecord & root,
                       std::vector<FieldInfo> const & fields,
                       std::vector<int> const & parent_map,
                       int field_idx,
                       ConcreteState const & state,
                       std::string const & value) {

    // Build ancestor chain from root to this field
    // Each entry: (field_index, which index in state.indices to use)
    std::vector<int> ancestors;
    int cur = field_idx;
    while (cur >= 0) {
        ancestors.push_back(cur);
        cur = parent_map[cur];
    }
    std::reverse(ancestors.begin(), ancestors.end());

    // Navigate from root, creating structure as we go
    // state.indices maps 1:1 with the ancestor chain depth
    FieldRecord * current = &root;

    for (size_t a = 0; a < ancestors.size(); ++a) {
        int fidx = ancestors[a];
        auto const & f = fields[fidx];
        bool is_last = (a + 1 == ancestors.size());

        if (is_last) {
            // Set the actual value
            if (f.is_list() && !state.indices.empty()) {
                int idx = state.indices.back();
                auto it = current->find(f.name);
                if (it == current->end() || !it->second.is_array()) {
                    (*current)[f.name] = FieldValue(FieldArray{});
                }
                auto & arr = (*current)[f.name].as_array();
                while (static_cast<int>(arr.size()) <= idx) {
                    arr.push_back(FieldValue(std::string("")));
                }
                arr[idx] = FieldValue(value);
            } else {
                (*current)[f.name] = FieldValue(value);
            }
        } else {
            // Navigate into intermediate record/array
            int idx = (a < state.indices.size()) ? state.indices[a] : 0;

            if (f.is_list()) {
                // Ensure array exists
                auto it = current->find(f.name);
                if (it == current->end() || !it->second.is_array()) {
                    (*current)[f.name] = FieldValue(FieldArray{});
                }
                auto & arr = (*current)[f.name].as_array();
                // Ensure element exists as a record
                while (static_cast<int>(arr.size()) <= idx) {
                    arr.push_back(FieldValue(FieldRecord{}));
                }
                // If element is not a record, replace it
                if (!arr[idx].is_record()) {
                    arr[idx] = FieldValue(FieldRecord{});
                }
                current = &arr[idx].as_record();
            } else {
                // Non-list intermediate: ensure record exists
                auto it = current->find(f.name);
                if (it == current->end() || !it->second.is_record()) {
                    (*current)[f.name] = FieldValue(FieldRecord{});
                }
                current = &(*current)[f.name].as_record();
            }
        }
    }
}

// Resolve a select index back to the actual value using the ChoiceFormat path
static std::string resolve_select(std::string const & index_str,
                                   ChoiceFormat const & cf,
                                   json const & content,
                                   Syntax const & syntax) {
    // Collect values by navigating the path (same as ravel_choices in instantiate)
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

    // Convert index string to integer
    int offset = syntax.prompt_zero_index ? 0 : 1;
    try {
        int idx = std::stoi(index_str) - offset;
        if (idx >= 0 && idx < static_cast<int>(current.size())) {
            auto const * val = current[idx];
            return val->is_string() ? val->get<std::string>() : val->dump();
        }
    } catch (...) {}
    return index_str; // fallback: return as-is
}

FieldRecord parse_text(PromptSTA const & prompt, Syntax const & syntax,
                        std::string const & text, json const * content) {
    FieldRecord result;

    auto parent_map = build_parent_map(prompt.fields);

    // Find "start:" marker — use the LAST occurrence
    // (the first may be inside the mechanics code block in the header)
    std::string start_marker = "start:" + syntax.field_separator;
    auto start_pos = text.rfind(start_marker);
    if (start_pos == std::string::npos) {
        start_pos = 0;
    } else {
        start_pos += start_marker.size();
    }

    std::string body = text.substr(start_pos);

    // Walk the STA sequence and match labels against the text
    size_t pos = 0;
    for (size_t s = 1; s < prompt.sequence.size(); ++s) {
        auto const & tag = prompt.sequence[s];
        auto it = prompt.states.find(tag);
        if (it == prompt.states.end()) continue;
        auto const & state = it->second;
        if (state.field < 0) continue;

        auto const & fld = prompt.fields[state.field];
        if (fld.is_record()) continue; // records have no value, only children

        std::string label = expected_label(state, prompt, syntax);

        // Find the label in the remaining text
        auto label_pos = body.find(label, pos);
        if (label_pos == std::string::npos) continue;

        // Value starts after the label
        size_t value_start = label_pos + label.size();

        // Value ends at the next field separator
        auto sep_pos = body.find(syntax.field_separator, value_start);
        std::string value;
        if (sep_pos != std::string::npos) {
            value = body.substr(value_start, sep_pos - value_start);
            pos = sep_pos + syntax.field_separator.size();
        } else {
            value = body.substr(value_start);
            pos = body.size();
        }

        // Resolve select choices: convert index back to actual value
        if (content) {
            auto const & fmt = fld.format;
            if (auto * cf = std::get_if<ChoiceFormat>(&fmt)) {
                if (cf->mode == "select") {
                    value = resolve_select(value, *cf, *content, syntax);
                }
            }
        }

        set_value(result, prompt.fields, parent_map, state.field, state, value);
    }

    // Parse flow control: "next: <choice>"
    std::string next_label = "next: ";
    auto next_pos = body.find(next_label, pos > 0 ? pos - syntax.field_separator.size() : 0);
    if (next_pos == std::string::npos) {
        next_pos = body.rfind(next_label);
    }
    if (next_pos != std::string::npos) {
        size_t value_start = next_pos + next_label.size();
        auto end_pos = body.find_first_of("\n\r", value_start);
        std::string flow;
        if (end_pos != std::string::npos) {
            flow = body.substr(value_start, end_pos - value_start);
        } else {
            flow = body.substr(value_start);
        }
        // Trim trailing whitespace
        while (!flow.empty() && (flow.back() == ' ' || flow.back() == '\n' || flow.back() == '\r')) {
            flow.pop_back();
        }
        result["next"] = FieldValue(flow);
    }

    return result;
}

// ============================================================================
// JSON conversion
// ============================================================================

json field_value_to_json(FieldValue const & val) {
    if (val.is_string()) {
        return val.as_string();
    } else if (val.is_record()) {
        return field_record_to_json(val.as_record());
    } else if (val.is_array()) {
        json arr = json::array();
        for (auto const & item : val.as_array()) {
            arr.push_back(field_value_to_json(item));
        }
        return arr;
    }
    return nullptr;
}

json field_record_to_json(FieldRecord const & rec) {
    json obj = json::object();
    for (auto const & [key, val] : rec) {
        obj[key] = field_value_to_json(val);
    }
    return obj;
}

}

#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic pop
#endif
