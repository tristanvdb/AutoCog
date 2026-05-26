
#include "autocog/compiler/stl/serialize.hxx"
#include "autocog/compiler/stl/driver.hxx"
#include "helpers.hxx"

namespace autocog::compiler::stl {

namespace sta = autocog::runtime::sta;
using json = serialize::json;

static json format_to_json(sta::FieldFormat const & fmt) {
    return std::visit([](auto const & f) -> json {
        using T = std::decay_t<decltype(f)>;
        if constexpr (std::is_same_v<T, std::monostate>) {
            return json{{"type", "record"}};
        } else if constexpr (std::is_same_v<T, sta::CompletionFormat>) {
            json j = {{"type", "completion"}};
            if (f.length)    j["length"]    = *f.length;
            if (f.threshold) j["threshold"] = *f.threshold;
            if (f.beams)     j["beams"]     = *f.beams;
            if (f.ahead)     j["ahead"]     = *f.ahead;
            if (f.width)     j["width"]     = *f.width;
            return j;
        } else if constexpr (std::is_same_v<T, sta::EnumFormat>) {
            json j = {{"type", "enum"}, {"values", f.values}};
            if (f.width) j["width"] = *f.width;
            return j;
        } else if constexpr (std::is_same_v<T, sta::ChoiceFormat>) {
            json path = json::array();
            for (auto const & [name, range] : f.path) {
                json step = {{"name", name}};
                if (range) step["range"] = json::array({range->first, range->second});
                else step["range"] = nullptr;
                path.push_back(step);
            }
            json j = {{"type", "choice"}, {"mode", f.mode}, {"path", path}};
            if (f.width) j["width"] = *f.width;
            return j;
        }
    }, fmt);
}

static json field_to_json(sta::FieldInfo const & f) {
    json j;
    j["name"] = f.name;
    j["depth"] = f.depth;
    j["index"] = f.index;
    j["flat_index"] = f.flat_index;
    j["desc"] = f.desc;
    if (f.range) j["range"] = json::array({f.range->first, f.range->second});
    else j["range"] = nullptr;
    j["format"] = format_to_json(f.format);
    return j;
}

static json state_to_json(sta::ConcreteState const & s) {
    json j;
    j["field"] = s.field;
    j["indices"] = s.indices;
    j["successors"] = s.successors;
    return j;
}

static json flow_entry_to_json(sta::FlowEntry const & entry) {
    return std::visit([](auto const & e) -> json {
        using T = std::decay_t<decltype(e)>;
        if constexpr (std::is_same_v<T, sta::FlowTarget>) {
            return json{{"type", "control"}, {"prompt", e.prompt}, {"limit", e.limit}};
        } else if constexpr (std::is_same_v<T, sta::ReturnTarget>) {
            json fields = json::array();
            for (auto const & rf : e.fields) {
                json path = json::array();
                for (auto const & [name, idx] : rf.path) {
                    json step = {{"name", name}};
                    if (idx) step["index"] = *idx;
                    path.push_back(step);
                }
                fields.push_back(json{{"alias", rf.alias}, {"path", path}});
            }
            return json{{"type", "return"}, {"fields", fields}};
        }
    }, entry);
}

static json pathstep_to_json(sta::PathStep const & s) {
    json j = {{"name", s.name}};
    if (s.index) j["index"] = *s.index;
    else j["index"] = nullptr;
    return j;
}

static json pathsteps_to_json(std::vector<sta::PathStep> const & steps) {
    json arr = json::array();
    for (auto const & s : steps) arr.push_back(pathstep_to_json(s));
    return arr;
}

static json clause_to_json(sta::Clause const & clause) {
    return std::visit([](auto const & c) -> json {
        using T = std::decay_t<decltype(c)>;
        if constexpr (std::is_same_v<T, sta::BindClause>) {
            return json{{"type","bind"}, {"source", pathsteps_to_json(c.source)}, {"target", pathsteps_to_json(c.target)}};
        } else if constexpr (std::is_same_v<T, sta::RavelClause>) {
            return json{{"type","ravel"}, {"depth", c.depth}, {"target", pathsteps_to_json(c.target)}};
        } else if constexpr (std::is_same_v<T, sta::WrapClause>) {
            return json{{"type","wrap"}, {"target", pathsteps_to_json(c.target)}};
        } else if constexpr (std::is_same_v<T, sta::PruneClause>) {
            return json{{"type","prune"}, {"target", pathsteps_to_json(c.target)}};
        } else if constexpr (std::is_same_v<T, sta::MappedClause>) {
            return json{{"type","mapped"}, {"target", pathsteps_to_json(c.target)}};
        }
    }, clause);
}

static json clauses_to_json(std::vector<sta::Clause> const & clauses) {
    json arr = json::array();
    for (auto const & c : clauses) arr.push_back(clause_to_json(c));
    return arr;
}

static json channel_to_json(sta::Channel const & ch) {
    return std::visit([](auto const & c) -> json {
        using T = std::decay_t<decltype(c)>;
        if constexpr (std::is_same_v<T, sta::InputChannel>) {
            return json{{"type","input"}, {"target", pathsteps_to_json(c.target)}, {"source", pathsteps_to_json(c.source)}};
        } else if constexpr (std::is_same_v<T, sta::DataflowChannel>) {
            json j = {{"type","dataflow"}, {"target", pathsteps_to_json(c.target)},
                      {"source", pathsteps_to_json(c.source)}, {"clauses", clauses_to_json(c.clauses)}};
            j["prompt"] = c.prompt ? json(*c.prompt) : json(nullptr);
            return j;
        } else if constexpr (std::is_same_v<T, sta::CallChannel>) {
            json kwargs = json::array();
            for (auto const & kw : c.kwargs) {
                json k = {{"name", kw.name}, {"is_input", kw.is_input},
                          {"path", pathsteps_to_json(kw.path)}, {"clauses", clauses_to_json(kw.clauses)}};
                k["prompt"] = kw.prompt ? json(*kw.prompt) : json(nullptr);
                kwargs.push_back(k);
            }
            json j = {{"type","call"}, {"target", pathsteps_to_json(c.target)},
                      {"kwargs", kwargs}, {"clauses", clauses_to_json(c.clauses)}};
            j["extern"] = c.extern_func ? json(*c.extern_func) : json(nullptr);
            j["entry"] = c.entry ? json(*c.entry) : json(nullptr);
            return j;
        }
    }, ch);
}

void serialize_sta(Driver const & driver, std::ostream & out) {
    json output;

    output["entry_points"] = json::object();
    for (auto const & [name, mangled] : driver.sta.entry_points) {
        output["entry_points"][name] = mangled;
    }

    output["prompts"] = json::object();
    for (auto const & [mangled, pstas] : driver.sta.prompts) {
        json p;
        p["name"] = pstas.name;
        p["desc"] = pstas.desc;

        p["fields"] = json::array();
        for (auto const & f : pstas.fields) {
            p["fields"].push_back(field_to_json(f));
        }

        p["states"] = json::object();
        for (auto const & [tag, state] : pstas.states) {
            p["states"][tag] = state_to_json(state);
        }

        p["sequence"] = pstas.sequence;

        p["flows"] = json::object();
        for (auto const & [name, entry] : pstas.flows) {
            p["flows"][name] = flow_entry_to_json(entry);
        }

        p["channels"] = json::array();
        for (auto const & ch : pstas.channels) {
            p["channels"].push_back(channel_to_json(ch));
        }

        output["prompts"][mangled] = p;
    }

    out << output.dump(2) << std::endl;
}

}
