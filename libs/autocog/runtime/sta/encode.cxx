#include "autocog/runtime/sta/encode.hxx"
#include "autocog/runtime/sta/walk.hxx"

#include "autocog/utilities/errors.hxx"
#include "autocog/utilities/exception.hxx"

#include <algorithm>
#include <map>
#include <string>
#include <variant>
#include <vector>

namespace autocog::runtime::sta {

namespace {

using Doc = autocog::types::Document;

bool doc_is_obj(Doc const & d) { return std::holds_alternative<Doc::Object>(d.value); }
bool doc_is_arr(Doc const & d) { return std::holds_alternative<Doc::Array>(d.value); }
bool doc_is_null(Doc const & d) {
    auto const * v = std::get_if<autocog::types::Value>(&d.value);
    return v && std::holds_alternative<std::monostate>(*v);
}

bool doc_equal(Doc const & a, Doc const & b) {
    if (a.value.index() != b.value.index()) return false;
    if (auto const * va = std::get_if<autocog::types::Value>(&a.value))
        return *va == std::get<autocog::types::Value>(b.value);
    if (auto const * aa = std::get_if<Doc::Array>(&a.value)) {
        auto const & ba = std::get<Doc::Array>(b.value);
        if (aa->size() != ba.size()) return false;
        for (size_t i = 0; i < aa->size(); ++i)
            if (!doc_equal((*aa)[i], ba[i])) return false;
        return true;
    }
    auto const & ao = std::get<Doc::Object>(a.value);
    auto const & bo = std::get<Doc::Object>(b.value);
    if (ao.size() != bo.size()) return false;
    for (auto const & [k, v] : ao) {
        auto it = bo.find(k);
        if (it == bo.end() || !doc_equal(v, it->second)) return false;
    }
    return true;
}

// A scalar frame value as the text it renders to. Frames produced by the
// walker hold strings; scalars of other types (content-typed values) render
// through their canonical spelling.
std::string doc_to_text(Doc const & d, std::string const & where) {
    auto const * v = std::get_if<autocog::types::Value>(&d.value);
    if (!v)
        throw autocog::ConfigError("Frame value for '" + where + "' is not a scalar", where);
    if (auto const * s = std::get_if<std::string>(v)) return *s;
    if (auto const * i = std::get_if<int>(v))         return std::to_string(*i);
    if (auto const * f = std::get_if<float>(v))       return std::to_string(*f);
    if (auto const * b = std::get_if<bool>(v))        return *b ? "true" : "false";
    throw autocog::ConfigError("Frame value for '" + where + "' is null", where);
}

// Read-only dual of walk.cxx's set_field_value: locate the value for
// (field_idx, indices) in the nested frame; nullptr when absent.
Doc const * get_field_value(Doc const & frame,
                            std::vector<autocog::data::Field> const & fields,
                            int field_idx,
                            std::vector<int> const & indices) {
    autocog::data::Field const & fld = fields[field_idx];

    if (fld.depth == 1) {
        if (!doc_is_obj(frame)) return nullptr;
        auto const & obj = std::get<Doc::Object>(frame.value);
        auto it = obj.find(fld.name);
        if (it == obj.end()) return nullptr;
        if (fld.is_list() && !indices.empty()) {
            if (!doc_is_arr(it->second)) return nullptr;
            auto const & arr = std::get<Doc::Array>(it->second.value);
            int const ai = indices.back();
            if (ai < 0 || ai >= static_cast<int>(arr.size())) return nullptr;
            return &arr[ai];
        }
        return &it->second;
    }

    // Nested: the ancestor chain child->root, one index per list field from
    // the end of `indices` (mirrors set_field_value exactly).
    struct Step { std::string name; int arr_idx; };
    std::vector<Step> chain;
    int idx = field_idx;
    int idx_cursor = static_cast<int>(indices.size());
    while (idx >= 0) {
        autocog::data::Field const & f = fields[idx];
        idx_cursor--;
        int const arr_idx = (idx_cursor >= 0) ? indices[idx_cursor] : 0;
        chain.push_back({f.name, f.is_list() ? arr_idx : -1});
        if (f.depth == 1) break;
        int parent_idx = idx - 1;
        while (parent_idx >= 0 && fields[parent_idx].depth >= f.depth) parent_idx--;
        idx = parent_idx;
    }
    std::reverse(chain.begin(), chain.end());

    Doc const * current = &frame;
    for (auto const & step : chain) {
        if (!doc_is_obj(*current)) return nullptr;
        auto const & obj = std::get<Doc::Object>(current->value);
        auto it = obj.find(step.name);
        if (it == obj.end()) return nullptr;
        current = &it->second;
        if (step.arr_idx >= 0) {
            if (!doc_is_arr(*current)) return nullptr;
            auto const & arr = std::get<Doc::Array>(current->value);
            if (step.arr_idx >= static_cast<int>(arr.size())) return nullptr;
            current = &arr[step.arr_idx];
        }
    }
    return current;
}

struct Encoder {
    autocog::data::FTA const & fta;
    std::vector<autocog::data::Field> const & fields;
    Doc const & frame;
    Doc const & content;
    std::map<std::string, unsigned> uid_to_index;

    Encoder(autocog::data::FTA const & fta_,
            std::vector<autocog::data::Field> const & fields_,
            Doc const & frame_, Doc const & content_)
        : fta(fta_), fields(fields_), frame(frame_), content(content_) {
        for (unsigned i = 0; i < fta.actions.size(); ++i)
            uid_to_index.emplace(fta.actions[i].uid, i);
    }

    unsigned resolve(std::string const & uid) const {
        auto it = uid_to_index.find(uid);
        if (it == uid_to_index.end())
            throw autocog::ConfigError("FTA references unknown action '" + uid + "'", uid);
        return it->second;
    }

    Doc const * value_of(autocog::data::Action const & act) const {
        if (!act.field) return nullptr;
        static std::vector<int> const no_indices;
        Doc const * v = get_field_value(frame, fields, *act.field,
                                        act.indices ? *act.indices : no_indices);
        return (v && !doc_is_null(*v)) ? v : nullptr;
    }

    // Whether the frame contains what this action (and the linear chain after
    // it) would render: the nearest reachable value action must have a value;
    // a nested structural choose is viable when any of its branches is; a
    // chain that ends without another value action is trivially viable.
    bool viable(unsigned id) const {
        unsigned a = id;
        for (size_t guard = 0; guard <= fta.actions.size(); ++guard) {
            autocog::data::Action const & act = fta.actions[a];
            if (act.field) return value_of(act) != nullptr;
            if (std::holds_alternative<autocog::data::ChooseAction>(act.body)) {
                for (auto const & succ : act.successors)
                    if (viable(resolve(succ))) return true;
                return false;
            }
            if (act.successors.empty()) return true;
            a = resolve(act.successors[0]);
        }
        throw autocog::utilities::InternalError("encode: cycle in FTA successor graph");
    }

    // Fill one FTT node for action `id`; returns the next action, if any.
    std::optional<unsigned> fill(autocog::data::FTTNode & n, unsigned id) const {
        autocog::data::Action const & act = fta.actions[id];
        n.action  = id;
        n.uid     = act.uid;
        n.field   = act.field;
        n.indices = act.indices;
        n.pruned  = autocog::data::Pruned::No;

        std::optional<unsigned> next;
        if (auto const * t = std::get_if<autocog::data::TextAction>(&act.body)) {
            n.text = t->text;
            if (!act.successors.empty()) next = resolve(act.successors[0]);
        } else if (std::get_if<autocog::data::CompleteAction>(&act.body)) {
            Doc const * v = value_of(act);
            if (!v)
                throw autocog::ConfigError("Frame has no value for completion '" + act.uid + "'", act.uid);
            n.text = doc_to_text(*v, act.uid);
            if (!act.successors.empty()) next = resolve(act.successors[0]);
        } else if (auto const * ch = std::get_if<autocog::data::ChooseAction>(&act.body)) {
            if (act.successors.size() != ch->choices.size())
                throw autocog::ConfigError("Choose '" + act.uid + "' successor/choice count mismatch", act.uid);
            size_t idx = ch->choices.size();
            if (act.field) {
                Doc const * v = value_of(act);
                if (!v)
                    throw autocog::ConfigError("Frame has no value for choice '" + act.uid + "'", act.uid);
                std::string const want = doc_to_text(*v, act.uid);
                for (size_t i = 0; i < ch->choices.size(); ++i)
                    if (ch->choices[i] == want) { idx = i; break; }
                if (idx == ch->choices.size()) {
                    // Engine-produced frames hold *resolved* select values;
                    // probe each index through the walker's resolver.
                    auto const * cf = std::get_if<autocog::data::ChoiceFormat>(
                        &fields[*act.field].format.value);
                    if (cf && cf->mode == "select") {
                        for (size_t i = 0; i < ch->choices.size(); ++i) {
                            if (doc_equal(resolve_select(ch->choices[i], *cf, content), *v)) {
                                idx = i;
                                break;
                            }
                        }
                    }
                }
                if (idx == ch->choices.size())
                    throw autocog::ConfigError("Frame value '" + want + "' matches no choice of '" + act.uid + "'", act.uid);
            } else {
                for (size_t i = 0; i < act.successors.size(); ++i)
                    if (viable(resolve(act.successors[i]))) { idx = i; break; }
                if (idx == ch->choices.size())
                    throw autocog::ConfigError("Frame leaves no viable branch at '" + act.uid + "'", act.uid);
            }
            n.text = ch->choices[idx];
            next = resolve(act.successors[idx]);
        }
        return next;
    }
};

}  // namespace

autocog::data::FTT encode_frame_to_ftt(
    autocog::data::FTA const & fta,
    autocog::data::STA const & sta,
    std::string const & prompt_name,
    autocog::types::Document const & frame,
    autocog::types::Document const & content
) {
    auto pit = sta.prompts.find(prompt_name);
    if (pit == sta.prompts.end())
        throw autocog::ConfigError("Prompt '" + prompt_name + "' not found in STA", prompt_name);
    if (fta.actions.empty())
        throw autocog::ConfigError("Cannot encode against an empty FTA", prompt_name);

    Encoder enc(fta, pit->second.fields, frame, content);

    autocog::data::FTT ftt;
    std::optional<unsigned> next = enc.fill(ftt.root, 0);
    autocog::data::FTTNode * node = &ftt.root;
    while (next) {
        node->children.emplace_back();
        node = &node->children.back();
        next = enc.fill(*node, *next);
    }
    return ftt;
}

}
