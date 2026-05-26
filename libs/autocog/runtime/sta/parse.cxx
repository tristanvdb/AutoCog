
#include "autocog/runtime/sta/parse.hxx"

#include <sstream>
#include <stdexcept>

namespace autocog::runtime::sta {

using json = nlohmann::json;

// ============================================================================
// Parse text → FieldRecord
// ============================================================================

// Build the expected label for a field+state, same as instantiate's prompt_label
static std::string expected_label(ConcreteState const & state,
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

// Set a value at a path in a record, creating intermediate records/arrays
static void set_value(FieldRecord & root, FieldInfo const & fld,
                       ConcreteState const & state, std::string const & value) {
    // For depth-1 scalar fields: root[name] = value
    // For depth-1 array fields: root[name][index] = value
    // For deeper fields: need parent tracking (simplified: use flat name for now)

    if (fld.is_list() && !state.indices.empty()) {
        int idx = state.indices.back();
        auto it = root.find(fld.name);
        if (it == root.end() || !it->second.is_array()) {
            root[fld.name] = FieldValue(FieldArray{});
        }
        auto & arr = root[fld.name].as_array();
        while (static_cast<int>(arr.size()) <= idx) {
            arr.push_back(FieldValue(std::string("")));
        }
        arr[idx] = FieldValue(value);
    } else {
        root[fld.name] = FieldValue(value);
    }
}

FieldRecord parse_text(PromptSTA const & prompt, Syntax const & syntax,
                        std::string const & text) {
    FieldRecord result;

    // Find "start:" marker
    std::string start_marker = "start:" + syntax.field_separator;
    auto start_pos = text.find(start_marker);
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

        set_value(result, fld, state, value);
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
