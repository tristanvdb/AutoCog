
#include "autocog/runtime/sta/load.hxx"

namespace autocog::runtime::sta {

using json = nlohmann::json;

static FieldFormat load_format(json const & j) {
    auto type = j["type"].get<std::string>();
    if (type == "record") return std::monostate{};
    if (type == "completion") {
        CompletionFormat f;
        if (j.contains("length"))    f.length    = j["length"].get<int>();
        if (j.contains("threshold")) f.threshold = j["threshold"].get<float>();
        if (j.contains("beams"))     f.beams     = j["beams"].get<int>();
        if (j.contains("ahead"))     f.ahead     = j["ahead"].get<int>();
        if (j.contains("width"))     f.width     = j["width"].get<int>();
        return f;
    }
    if (type == "enum") {
        EnumFormat f;
        for (auto const & v : j["values"]) f.values.push_back(v.get<std::string>());
        if (j.contains("width") && !j["width"].is_null()) f.width = j["width"].get<int>();
        return f;
    }
    if (type == "choice") {
        ChoiceFormat f;
        f.mode = j["mode"].get<std::string>();
        if (j.contains("width") && !j["width"].is_null()) f.width = j["width"].get<int>();
        for (auto const & step : j["path"]) {
            std::string name = step["name"];
            std::optional<std::pair<int,int>> range;
            if (!step["range"].is_null()) {
                auto r = step["range"];
                range = std::make_pair(r[0].get<int>(), r[1].get<int>());
            }
            f.path.emplace_back(name, range);
        }
        return f;
    }
    return std::monostate{};
}

PromptSTA load_prompt(json const & j) {
    PromptSTA p;
    p.name = j["name"];
    for (auto const & d : j["desc"]) p.desc.push_back(d);

    for (auto const & fj : j["fields"]) {
        FieldInfo fi;
        fi.name = fj["name"];
        fi.depth = fj["depth"];
        fi.index = fj["index"];
        fi.flat_index = fj.value("flat_index", static_cast<int>(p.fields.size()));
        for (auto const & d : fj["desc"]) fi.desc.push_back(d);
        if (!fj["range"].is_null()) {
            fi.range = std::make_pair(fj["range"][0].get<int>(), fj["range"][1].get<int>());
        }
        fi.format = load_format(fj["format"]);
        p.fields.push_back(std::move(fi));
    }

    for (auto const & [tag, sj] : j["states"].items()) {
        ConcreteState cs;
        cs.tag = tag;
        cs.field = sj["field"];
        for (auto const & i : sj["indices"]) cs.indices.push_back(i);
        for (auto const & s : sj["successors"]) cs.successors.push_back(s);
        p.states[tag] = std::move(cs);
    }

    for (auto const & s : j["sequence"]) p.sequence.push_back(s);

    for (auto const & [name, fj] : j["flows"].items()) {
        auto type = fj["type"].get<std::string>();
        if (type == "control") {
            p.flows[name] = FlowTarget{fj["prompt"], fj["limit"]};
        } else if (type == "return") {
            ReturnTarget rt;
            for (auto const & rf : fj["fields"]) {
                ReturnField srf;
                srf.alias = rf["alias"];
                for (auto const & step : rf["path"]) {
                    std::optional<int> idx;
                    if (step.contains("index")) idx = step["index"].get<int>();
                    srf.path.emplace_back(step["name"].get<std::string>(), idx);
                }
                rt.fields.push_back(std::move(srf));
            }
            p.flows[name] = std::move(rt);
        }
    }

    // Load channels
    if (j.contains("channels")) {
        for (auto const & cj : j["channels"]) {
            auto type = cj["type"].get<std::string>();

            auto load_pathsteps = [](json const & arr) -> std::vector<PathStep> {
                std::vector<PathStep> steps;
                for (auto const & s : arr) {
                    PathStep ps;
                    ps.name = s["name"];
                    if (s.contains("index") && !s["index"].is_null()) ps.index = s["index"].get<int>();
                    steps.push_back(std::move(ps));
                }
                return steps;
            };

            auto load_clauses = [&](json const & arr) -> std::vector<Clause> {
                std::vector<Clause> clauses;
                for (auto const & cl : arr) {
                    auto ct = cl["type"].get<std::string>();
                    if (ct == "bind") {
                        BindClause bc;
                        bc.source = load_pathsteps(cl["source"]);
                        bc.target = load_pathsteps(cl["target"]);
                        clauses.push_back(std::move(bc));
                    } else if (ct == "ravel") {
                        RavelClause rc;
                        rc.depth = cl.value("depth", 1);
                        rc.target = load_pathsteps(cl["target"]);
                        clauses.push_back(std::move(rc));
                    } else if (ct == "wrap") {
                        WrapClause wc;
                        wc.target = load_pathsteps(cl["target"]);
                        clauses.push_back(std::move(wc));
                    } else if (ct == "prune") {
                        PruneClause pc;
                        pc.target = load_pathsteps(cl["target"]);
                        clauses.push_back(std::move(pc));
                    } else if (ct == "mapped") {
                        MappedClause mc;
                        mc.target = load_pathsteps(cl["target"]);
                        clauses.push_back(std::move(mc));
                    }
                }
                return clauses;
            };

            if (type == "input") {
                InputChannel ic;
                ic.target = load_pathsteps(cj["target"]);
                ic.source = load_pathsteps(cj["source"]);
                p.channels.push_back(std::move(ic));
            } else if (type == "dataflow") {
                DataflowChannel dc;
                dc.target = load_pathsteps(cj["target"]);
                dc.source = load_pathsteps(cj["source"]);
                if (cj.contains("prompt") && !cj["prompt"].is_null()) dc.prompt = cj["prompt"];
                dc.clauses = load_clauses(cj["clauses"]);
                p.channels.push_back(std::move(dc));
            } else if (type == "call") {
                CallChannel cc;
                cc.target = load_pathsteps(cj["target"]);
                if (cj.contains("extern") && !cj["extern"].is_null()) cc.extern_func = cj["extern"];
                if (cj.contains("entry") && !cj["entry"].is_null()) cc.entry = cj["entry"];
                for (auto const & kj : cj["kwargs"]) {
                    ChannelKwarg ck;
                    ck.name = kj["name"];
                    ck.is_input = kj["is_input"];
                    if (kj.contains("prompt") && !kj["prompt"].is_null()) ck.prompt = kj["prompt"];
                    if (kj.contains("value") && !kj["value"].is_null()) ck.value = kj["value"];
                    ck.path = load_pathsteps(kj["path"]);
                    ck.clauses = load_clauses(kj["clauses"]);
                    cc.kwargs.push_back(std::move(ck));
                }
                cc.clauses = load_clauses(cj["clauses"]);
                p.channels.push_back(std::move(cc));
            }
        }
    }

    return p;
}

Program load_program(json const & j) {
    Program prog;
    for (auto const & [name, mangled] : j["entry_points"].items()) {
        prog.entry_points[name] = mangled;
    }
    if (j.contains("python_imports")) {
        for (auto const & [name, imp] : j["python_imports"].items()) {
            prog.python_imports[name] = PythonImport{
                imp.value("file", ""),
                imp.value("target", name)
            };
        }
    }
    for (auto const & [name, pj] : j["prompts"].items()) {
        prog.prompts[name] = load_prompt(pj);
    }
    return prog;
}

}

namespace autocog::runtime::sta {

using json = nlohmann::json;

// ============================================================================
// Serialize Program to JSON
// ============================================================================

static json format_to_json(FieldFormat const & fmt) {
    return std::visit([](auto const & f) -> json {
        using T = std::decay_t<decltype(f)>;
        if constexpr (std::is_same_v<T, std::monostate>) return json{{"type","record"}};
        else if constexpr (std::is_same_v<T, CompletionFormat>) {
            json j = {{"type","completion"}};
            if (f.length) j["length"] = *f.length;
            if (f.threshold) j["threshold"] = *f.threshold;
            if (f.beams) j["beams"] = *f.beams;
            if (f.ahead) j["ahead"] = *f.ahead;
            if (f.width) j["width"] = *f.width;
            return j;
        }
        else if constexpr (std::is_same_v<T, EnumFormat>) {
            json j = {{"type","enum"}, {"values", f.values}};
            if (f.width) j["width"] = *f.width;
            return j;
        }
        else if constexpr (std::is_same_v<T, ChoiceFormat>) {
            json path = json::array();
            for (auto const & [name, range] : f.path) {
                json step = {{"name", name}};
                if (range) step["range"] = json::array({range->first, range->second});
                else step["range"] = nullptr;
                path.push_back(step);
            }
            json j = {{"type","choice"}, {"mode", f.mode}, {"path", path}};
            if (f.width) j["width"] = *f.width;
            return j;
        }
        return json{{"type","unknown"}};
    }, fmt);
}

static json pathsteps_to_json(std::vector<PathStep> const & steps) {
    json arr = json::array();
    for (auto const & s : steps) {
        json j = {{"name", s.name}};
        j["index"] = s.index ? json(*s.index) : json(nullptr);
        arr.push_back(j);
    }
    return arr;
}

static json clause_to_json(Clause const & clause) {
    return std::visit([](auto const & c) -> json {
        using T = std::decay_t<decltype(c)>;
        if constexpr (std::is_same_v<T, BindClause>)
            return json{{"type","bind"}, {"source", pathsteps_to_json(c.source)}, {"target", pathsteps_to_json(c.target)}};
        else if constexpr (std::is_same_v<T, RavelClause>)
            return json{{"type","ravel"}, {"depth", c.depth}, {"target", pathsteps_to_json(c.target)}};
        else if constexpr (std::is_same_v<T, WrapClause>)
            return json{{"type","wrap"}, {"target", pathsteps_to_json(c.target)}};
        else if constexpr (std::is_same_v<T, PruneClause>)
            return json{{"type","prune"}, {"target", pathsteps_to_json(c.target)}};
        else if constexpr (std::is_same_v<T, MappedClause>)
            return json{{"type","mapped"}, {"target", pathsteps_to_json(c.target)}};
    }, clause);
}

static json clauses_to_json(std::vector<Clause> const & clauses) {
    json arr = json::array();
    for (auto const & c : clauses) arr.push_back(clause_to_json(c));
    return arr;
}

static json channel_to_json(Channel const & ch) {
    return std::visit([](auto const & c) -> json {
        using T = std::decay_t<decltype(c)>;
        if constexpr (std::is_same_v<T, InputChannel>)
            return json{{"type","input"}, {"target", pathsteps_to_json(c.target)}, {"source", pathsteps_to_json(c.source)}};
        else if constexpr (std::is_same_v<T, DataflowChannel>) {
            json j = {{"type","dataflow"}, {"target", pathsteps_to_json(c.target)},
                      {"source", pathsteps_to_json(c.source)}, {"clauses", clauses_to_json(c.clauses)}};
            j["prompt"] = c.prompt ? json(*c.prompt) : json(nullptr);
            return j;
        }
        else if constexpr (std::is_same_v<T, CallChannel>) {
            json kwargs = json::array();
            for (auto const & kw : c.kwargs) {
                json k = {{"name", kw.name}, {"is_input", kw.is_input},
                          {"path", pathsteps_to_json(kw.path)}, {"clauses", clauses_to_json(kw.clauses)}};
                k["prompt"] = kw.prompt ? json(*kw.prompt) : json(nullptr);
                k["value"] = kw.value ? json(*kw.value) : json(nullptr);
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

json serialize_prompt(PromptSTA const & p) {
    json j;
    j["name"] = p.name;
    j["desc"] = p.desc;
    j["fields"] = json::array();
    for (auto const & f : p.fields) {
        json fj;
        fj["name"] = f.name; fj["depth"] = f.depth; fj["index"] = f.index;
        fj["flat_index"] = f.flat_index; fj["desc"] = f.desc;
        fj["range"] = f.range ? json::array({f.range->first, f.range->second}) : json(nullptr);
        fj["format"] = format_to_json(f.format);
        j["fields"].push_back(fj);
    }
    j["states"] = json::object();
    for (auto const & [tag, state] : p.states) {
        j["states"][tag] = json{{"field", state.field}, {"indices", state.indices}, {"successors", state.successors}};
    }
    j["sequence"] = p.sequence;
    j["flows"] = json::object();
    for (auto const & [name, entry] : p.flows) {
        std::visit([&](auto const & e) {
            using T = std::decay_t<decltype(e)>;
            if constexpr (std::is_same_v<T, FlowTarget>)
                j["flows"][name] = json{{"type","control"}, {"prompt", e.prompt}, {"limit", e.limit}};
            else if constexpr (std::is_same_v<T, ReturnTarget>) {
                json fields = json::array();
                for (auto const & rf : e.fields) {
                    json path = json::array();
                    for (auto const & [n, idx] : rf.path) {
                        json step = {{"name", n}};
                        if (idx) step["index"] = *idx;
                        path.push_back(step);
                    }
                    fields.push_back(json{{"alias", rf.alias}, {"path", path}});
                }
                j["flows"][name] = json{{"type","return"}, {"fields", fields}};
            }
        }, entry);
    }
    j["channels"] = json::array();
    for (auto const & ch : p.channels) j["channels"].push_back(channel_to_json(ch));
    return j;
}

json serialize_program(Program const & prog) {
    json output;
    output["entry_points"] = json::object();
    for (auto const & [name, mangled] : prog.entry_points) output["entry_points"][name] = mangled;
    output["python_imports"] = json::object();
    for (auto const & [name, imp] : prog.python_imports) {
        output["python_imports"][name] = {{"file", imp.file}, {"target", imp.target}};
    }
    output["prompts"] = json::object();
    for (auto const & [name, p] : prog.prompts) output["prompts"][name] = serialize_prompt(p);
    return output;
}

} // namespace autocog::runtime::sta
