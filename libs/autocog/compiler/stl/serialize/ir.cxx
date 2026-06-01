
#include "autocog/compiler/stl/serialize.hxx"
#include "autocog/compiler/stl/driver.hxx"
#include "autocog/utilities/metadata.hxx"
#include "helpers.hxx"

#include <fstream>
#include <sstream>

namespace autocog::compiler::stl {

using json = serialize::json;

static json range_to_json(ir::Range const & range) {
    if (!range.has_value()) return nullptr;
    auto [start, end] = range.value();
    if (start == end) return json::array({start});
    return json::array({start, end});
}

static json pathstep_to_json(ir::PathStep const & step) {
    json j;
    j["name"] = step.name;
    // Selector: absent -> whole field; int -> index (scalar); StepRange -> slice
    // (list), with null for open bounds.
    if (step.selector.has_value()) {
        if (std::holds_alternative<int>(*step.selector)) {
            j["index"] = std::get<int>(*step.selector);
        } else {
            auto const & r = std::get<ir::StepRange>(*step.selector);
            j["slice"] = json::array({
                r.lower.has_value() ? json(*r.lower) : json(nullptr),
                r.upper.has_value() ? json(*r.upper) : json(nullptr)
            });
        }
    }
    return j;
}

static json docpath_to_json(ir::DocPath const & path) {
    json j;
    j["is_input"] = path.is_input;
    j["prompt"] = path.prompt.has_value() ? json(path.prompt.value()) : json(nullptr);
    j["steps"] = json::array();
    for (auto const & step : path.steps) {
        j["steps"].push_back(pathstep_to_json(step));
    }
    return j;
}

static json pathsteps_to_json(std::vector<ir::PathStep> const & steps) {
    json j = json::array();
    for (auto const & step : steps) {
        j.push_back(pathstep_to_json(step));
    }
    return j;
}

static json clause_to_json(ir::Clause const & clause) {
    return std::visit([](auto && c) -> json {
        using T = std::decay_t<decltype(c)>;
        json j;
        if constexpr (std::is_same_v<T, ir::BindClause>) {
            j["type"] = "bind";
            j["source"] = pathsteps_to_json(c.source);
            j["target"] = pathsteps_to_json(c.target);
        } else if constexpr (std::is_same_v<T, ir::RavelClause>) {
            j["type"] = "ravel";
            j["depth"] = c.depth.has_value() ? json(c.depth.value()) : json(nullptr);
            j["target"] = pathsteps_to_json(c.target);
        } else if constexpr (std::is_same_v<T, ir::WrapClause>) {
            j["type"] = "wrap";
            j["target"] = pathsteps_to_json(c.target);
        } else if constexpr (std::is_same_v<T, ir::PruneClause>) {
            j["type"] = "prune";
            j["target"] = pathsteps_to_json(c.target);
        } else if constexpr (std::is_same_v<T, ir::MappedClause>) {
            j["type"] = "mapped";
            j["target"] = pathsteps_to_json(c.target);
        }
        return j;
    }, clause);
}

static json field_to_json(ir::Field const & field);
static json search_to_json(ir::SearchPolicies const & search);

static json format_to_json(ir::Format const & format) {
    json j;
    std::visit([&](auto const & fmt) {
        using T = std::decay_t<decltype(fmt)>;
        if constexpr (std::is_same_v<T, ir::Completion>) {
            j["type"] = "completion";
            if (fmt.length.has_value()) j["length"] = fmt.length.value();
            if (fmt.vocab.has_value()) j["vocab"] = fmt.vocab.value();
        } else if constexpr (std::is_same_v<T, ir::Enum>) {
            j["type"] = "enum";
            j["values"] = fmt.values;
        } else if constexpr (std::is_same_v<T, ir::Choice>) {
            j["type"] = "choice";
            j["mode"] = fmt.mode;
            j["path"] = docpath_to_json(fmt.path);
        } else if constexpr (std::is_same_v<T, std::vector<std::unique_ptr<ir::Field>>>) {
            j["type"] = "struct";
            j["fields"] = json::array();
            for (auto const & field : fmt) {
                j["fields"].push_back(field_to_json(*field));
            }
        }
    }, format);
    return j;
}

static json field_to_json(ir::Field const & field) {
    json j;
    j["name"] = field.name;
    j["desc"] = field.desc;
    j["depth"] = field.depth;
    j["index"] = field.index;
    j["range"] = range_to_json(field.range);
    j["format"] = format_to_json(field.format);
    if (field.refname.has_value()) j["refname"] = field.refname.value();
    if (!field.refargs.empty()) j["refargs"] = serialize::varmap_to_json(field.refargs);
    if (!field.search.empty()) j["search"] = search_to_json(field.search);
    return j;
}

static json channel_to_json(ir::Channel const & channel) {
    return std::visit([](auto const & ch) -> json {
        using T = std::decay_t<decltype(ch)>;
        json j;

        if constexpr (std::is_same_v<T, ir::InputChannel>) {
            j["type"] = "input";
            j["target"] = docpath_to_json(ch.target);
            j["source"] = json::array();
            for (auto const & step : ch.source) {
                j["source"].push_back(pathstep_to_json(step));
            }
        } else if constexpr (std::is_same_v<T, ir::DataflowChannel>) {
            j["type"] = "dataflow";
            j["target"] = docpath_to_json(ch.target);
            j["prompt"] = ch.prompt.has_value() ? json(ch.prompt.value()) : json(nullptr);
            j["source"] = json::array();
            for (auto const & step : ch.source) {
                j["source"].push_back(pathstep_to_json(step));
            }
            j["clauses"] = json::array();
            for (auto const & clause : ch.clauses) {
                j["clauses"].push_back(clause_to_json(clause));
            }
        } else if constexpr (std::is_same_v<T, ir::CallChannel>) {
            j["type"] = "call";
            j["target"] = docpath_to_json(ch.target);
            j["extern"] = ch.extern_func.has_value() ? json(ch.extern_func.value()) : json(nullptr);
            j["entry"] = ch.entry.has_value() ? json(ch.entry.value()) : json(nullptr);
            j["kwargs"] = json::object();
            for (auto const & [name, kwarg] : ch.kwargs) {
                json kj;
                kj["is_input"] = kwarg.is_input;
                kj["prompt"] = kwarg.prompt.has_value() ? json(kwarg.prompt.value()) : json(nullptr);
                kj["path"] = json::array();
                for (auto const & step : kwarg.path) {
                    kj["path"].push_back(pathstep_to_json(step));
                }
                kj["clauses"] = json::array();
                for (auto const & clause : kwarg.clauses) {
                    kj["clauses"].push_back(clause_to_json(clause));
                }
                j["kwargs"][name] = kj;
            }
            if (ch.binds.has_value()) {
                j["binds"] = json::object();
                for (auto const & [name, path] : ch.binds.value()) {
                    json pj = json::array();
                    for (auto const & step : path) {
                        pj.push_back(pathstep_to_json(step));
                    }
                    j["binds"][name] = pj;
                }
            } else {
                j["binds"] = nullptr;
            }
            j["link_clauses"] = json::array();
            for (auto const & clause : ch.link_clauses) {
                j["link_clauses"].push_back(clause_to_json(clause));
            }
        }

        return j;
    }, channel);
}

static json flow_to_json(ir::FlowEdge const & flow) {
    json j;
    j["target"] = flow.target_prompt;
    j["label"] = flow.label.has_value() ? json(flow.label.value()) : json(nullptr);
    j["limit"] = flow.limit.has_value() ? json(flow.limit.value()) : json(nullptr);
    return j;
}

static json return_to_json(ir::ReturnInfo const & ret) {
    json j;
    j["label"] = ret.label.has_value() ? json(ret.label.value()) : json(nullptr);
    j["fields"] = json::array();
    for (auto const & field : ret.fields) {
        json fj;
        fj["source"] = docpath_to_json(field.source);
        fj["alias"] = field.alias.has_value() ? json(field.alias.value()) : json(nullptr);
        if (field.constant.has_value()) {
            fj["constant"] = field.constant.value();
        }
        fj["clauses"] = json::array();
        for (auto const & clause : field.clauses) {
            fj["clauses"].push_back(clause_to_json(clause));
        }
        j["fields"].push_back(fj);
    }
    return j;
}

static json search_to_json(ir::SearchPolicies const & search) {
    // Emit only when non-empty; nested as { category: { param: value } }.
    json j = json::object();
    for (auto const & [category, params] : search) {
        j[category] = serialize::varmap_to_json(params);
    }
    return j;
}

static json record_to_json(ir::Record const & record) {
    json j;
    j["name"] = record.name;
    j["desc"] = record.desc;
    if (!record.search.empty()) j["search"] = search_to_json(record.search);
    if (!record.vocabs.empty()) {
        json vj = json::object();
        for (auto const & [key, ve] : record.vocabs) vj[key] = ir::vocab_to_json(ve);
        j["vocabs"] = vj;
    }
    j["fields"] = json::array();
    for (auto const & field : record.fields) {
        j["fields"].push_back(field_to_json(*field));
    }
    return j;
}

static json prompt_to_json(ir::Prompt const & prompt) {
    json j;
    j["name"] = prompt.name;
    j["desc"] = prompt.desc;
    j["mangled_name"] = prompt.mangled_name;
    j["context"] = serialize::varmap_to_json(prompt.context);
    if (!prompt.search.empty()) j["search"] = search_to_json(prompt.search);
    if (!prompt.vocabs.empty()) {
        json vj = json::object();
        for (auto const & [key, ve] : prompt.vocabs) vj[key] = ir::vocab_to_json(ve);
        j["vocabs"] = vj;
    }
    j["fields"] = json::array();
    for (auto const & field : prompt.fields) {
        j["fields"].push_back(field_to_json(*field));
    }
    j["channels"] = json::array();
    for (auto const & channel : prompt.channels) {
        j["channels"].push_back(channel_to_json(channel));
    }
    j["flows"] = json::array();
    for (auto const & flow : prompt.flows) {
        j["flows"].push_back(flow_to_json(flow));
    }
    j["return"] = prompt.return_info.has_value() ? return_to_json(prompt.return_info.value()) : json(nullptr);
    return j;
}

void serialize_ir(Driver const & driver, std::ostream & out) {
    json output;

    output["defines"] = serialize::varmap_to_json(driver.defines);

    output["entry_points"] = json::object();
    for (auto const & [entry_name, mangled] : driver.entry_point_map) {
        output["entry_points"][entry_name] = mangled;
    }

    output["records"] = json::object();
    for (auto const & [mangled_name, record] : driver.records) {
        output["records"][mangled_name] = record_to_json(*record);
    }

    output["prompts"] = json::object();
    for (auto const & [mangled_name, prompt] : driver.prompts) {
        output["prompts"][mangled_name] = prompt_to_json(*prompt);
    }

    // Attach metadata
    if (!driver.inputs.empty()) {
        std::ifstream ifs(driver.inputs.front());
        std::ostringstream ss;
        ss << ifs.rdbuf();
        autocog::attach_metadata(output, "ir", autocog::compute_source_uid(ss.str()));
    }

    out << output.dump(2) << std::endl;
}

}
