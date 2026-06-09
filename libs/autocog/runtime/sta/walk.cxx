#include "autocog/runtime/sta/walk.hxx"
#include "autocog/utilities/errors.hxx"

#include <algorithm>
#include <cmath>
#include <utility>
#include <variant>
#include <vector>

namespace autocog::runtime::sta {

using Doc = autocog::types::Document;
using Val = autocog::types::Value;

namespace {

// --- Document helpers --------------------------------------------------------
Doc null_doc() { return Doc{ Val{ std::monostate{} } }; }
Doc str_doc(std::string s) { return Doc{ Val{ std::move(s) } }; }

bool is_obj(Doc const & d) { return std::holds_alternative<Doc::Object>(d.value); }
bool is_arr(Doc const & d) { return std::holds_alternative<Doc::Array>(d.value); }
bool doc_is_null(Doc const & d) {
    auto const * v = std::get_if<Val>(&d.value);
    return v && std::holds_alternative<std::monostate>(*v);
}
Doc::Object & as_object(Doc & d) {
    if (!is_obj(d)) d.value = Doc::Object{};
    return std::get<Doc::Object>(d.value);
}
Doc::Array & as_array(Doc & d) {
    if (!is_arr(d)) d.value = Doc::Array{};
    return std::get<Doc::Array>(d.value);
}

// Collect every root->leaf path that terminates on a non-pruned (completed)
// leaf, so a metric can compare all surviving leaves.
void collect_complete_paths(autocog::data::FTTNode const & node,
                            std::vector<autocog::data::FTTNode const *> & current,
                            std::vector<std::vector<autocog::data::FTTNode const *>> & paths) {
    current.push_back(&node);
    if (node.children.empty()) {
        if (!node.pruned) paths.push_back(current);
    } else {
        for (auto const & child : node.children) {
            collect_complete_paths(child, current, paths);
        }
    }
    current.pop_back();
}

// Score a completed path by its leaf, matching FTT proba():
// exp(-cumulative_logprob / total_length). Higher is better.
double leaf_score(autocog::data::FTTNode const & leaf) {
    double logprob = leaf.logprob;
    double length  = leaf.length;
    if (length <= 0.0) return 0.0;
    return std::exp(-logprob / length);
}

// Resolve a `select` choice index to the actual value from content, by walking
// the choice format's path into the content document. On any miss, the raw
// index string is returned unchanged.
Doc resolve_select(std::string const & index_str,
                   autocog::data::ChoiceFormat const & cf,
                   Doc const & content) {
    int idx;
    try {
        size_t consumed = 0;
        idx = std::stoi(index_str, &consumed);
        if (consumed != index_str.size()) return str_doc(index_str);  // not a clean int
    } catch (...) {
        return str_doc(index_str);
    }

    Doc const * current = &content;
    Doc gathered;   // backing storage for array-gather steps
    for (auto const & step : cf.path) {
        if (is_obj(*current)) {
            auto const & o = std::get<Doc::Object>(current->value);
            auto it = o.find(step.name);
            if (it == o.end()) return str_doc(index_str);
            current = &it->second;
        } else if (is_arr(*current)) {
            Doc::Array collected;
            for (auto const & item : std::get<Doc::Array>(current->value)) {
                if (is_obj(item)) {
                    auto const & io = std::get<Doc::Object>(item.value);
                    auto it = io.find(step.name);
                    collected.push_back(it != io.end() ? it->second : null_doc());
                } else {
                    collected.push_back(item);
                }
            }
            gathered = Doc{ std::move(collected) };
            current = &gathered;
        } else {
            return str_doc(index_str);
        }
        if (doc_is_null(*current)) return str_doc(index_str);
    }

    if (is_arr(*current)) {
        auto const & arr = std::get<Doc::Array>(current->value);
        if (idx >= 0 && idx < static_cast<int>(arr.size())) return arr[idx];
    }
    return str_doc(index_str);
}

// Set a value into the nested frame using the field hierarchy (depth + indices).
void set_field_value(Doc & frame,
                     std::vector<autocog::data::Field> const & fields,
                     int field_idx,
                     std::vector<int> const & indices,
                     Doc const & value) {
    autocog::data::Field const & fld = fields[field_idx];

    if (fld.depth == 1) {
        if (fld.is_list() && !indices.empty()) {
            int arr_idx = indices.back();
            auto & arr = as_array(as_object(frame)[fld.name]);
            while (static_cast<int>(arr.size()) <= arr_idx) arr.push_back(null_doc());
            arr[arr_idx] = value;
        } else {
            as_object(frame)[fld.name] = value;
        }
        return;
    }

    // Nested: build the ancestor chain child->root, consuming one index per
    // field from the end of `indices`.
    struct Step { std::string name; int arr_idx; };  // arr_idx = -1 if scalar
    std::vector<Step> chain;
    int idx = field_idx;
    int idx_cursor = static_cast<int>(indices.size());

    while (idx >= 0) {
        autocog::data::Field const & f = fields[idx];
        idx_cursor--;
        int arr_idx = (idx_cursor >= 0) ? indices[idx_cursor] : 0;
        chain.push_back({f.name, f.is_list() ? arr_idx : -1});
        if (f.depth == 1) break;
        int parent_idx = idx - 1;
        while (parent_idx >= 0 && fields[parent_idx].depth >= f.depth) parent_idx--;
        idx = parent_idx;
    }
    std::reverse(chain.begin(), chain.end());

    Doc * current = &frame;
    for (size_t i = 0; i < chain.size(); ++i) {
        auto const & step = chain[i];
        bool is_last = (i == chain.size() - 1);
        auto & obj = as_object(*current);
        if (is_last) {
            if (step.arr_idx >= 0) {
                auto & arr = as_array(obj[step.name]);
                while (static_cast<int>(arr.size()) <= step.arr_idx) arr.push_back(null_doc());
                arr[step.arr_idx] = value;
            } else {
                obj[step.name] = value;
            }
        } else {
            if (step.arr_idx >= 0) {
                auto & arr = as_array(obj[step.name]);
                while (static_cast<int>(arr.size()) <= step.arr_idx) arr.push_back(Doc{ Doc::Object{} });
                current = &arr[step.arr_idx];
            } else {
                Doc & child = obj[step.name];
                as_object(child);
                current = &child;
            }
        }
    }
}

} // namespace

autocog::types::Document walk_ftt_to_frame(
    autocog::data::FTT const & ftt,
    autocog::data::STA const & sta,
    std::string const & prompt_name,
    autocog::types::Document const & content,
    bool resolve_selects,
    std::string const & metric
) {
    auto const & prompt = sta.prompts.at(prompt_name);
    auto const & fields = prompt.fields;

    if (metric != "best")
        throw autocog::ConfigError("Unknown FTT scoring metric: " + metric, metric);

    // Gather every completed (non-pruned-leaf) path, then let the metric choose.
    std::vector<std::vector<autocog::data::FTTNode const *>> paths;
    {
        std::vector<autocog::data::FTTNode const *> current;
        collect_complete_paths(ftt.root, current, paths);
    }

    // "best": the completed path whose leaf has the highest score.
    std::vector<autocog::data::FTTNode const *> path;
    double best = -1.0;
    for (auto const & candidate : paths) {
        double s = leaf_score(*candidate.back());
        if (s > best) { best = s; path = candidate; }
    }

    Doc frame{ Doc::Object{} };
    for (auto const * node : path) {
        if (!node->field) continue;  // header/branch/endl: no value

        int field_idx = *node->field;
        autocog::data::Field const & fld = fields[field_idx];

        std::string raw = node->text;
        while (!raw.empty() && (raw.back() == '\n' || raw.back() == '\r')) raw.pop_back();

        Doc value = str_doc(raw);
        if (resolve_selects && !doc_is_null(content)) {
            if (auto const * cf = std::get_if<autocog::data::ChoiceFormat>(&fld.format.value)) {
                if (cf->mode == "select") {
                    value = resolve_select(raw, *cf, content);
                }
            }
        }

        std::vector<int> indices;
        if (node->indices) {
            for (int ix : *node->indices) indices.push_back(ix);
        }

        set_field_value(frame, fields, field_idx, indices, value);
    }

    return frame;
}

} // namespace autocog::runtime::sta
