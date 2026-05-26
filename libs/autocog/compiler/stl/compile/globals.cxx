
#include "autocog/compiler/stl/driver.hxx"
#include "autocog/compiler/stl/evaluate.hxx"

#include <tuple>
#include <vector>
#include <stdexcept>

namespace autocog::compiler::stl {

static std::tuple<int, std::optional<std::string>, std::string> parse_scope(std::string const & scope) {
    auto first_delim = scope.find("::");
    if (first_delim == std::string::npos) {
        throw std::runtime_error("Invalid scope format: " + scope);
    }

    int fid;
    try {
        fid = std::stoi(scope.substr(0, first_delim));
    } catch (...) {
        throw std::runtime_error("Invalid fileid in scope: " + scope);
    }

    auto rest = scope.substr(first_delim + 2);
    auto second_delim = rest.find("::");

    if (second_delim == std::string::npos) {
        if (rest.empty()) {
            throw std::runtime_error("Empty name in scope: " + scope);
        }
        return {fid, std::nullopt, rest};
    } else {
        auto object = rest.substr(0, second_delim);
        auto name = rest.substr(second_delim + 2);
        if (object.empty() || name.empty()) {
            throw std::runtime_error("Empty object or name in scope: " + scope);
        }
        return {fid, object, name};
    }
}

std::optional<int> Driver::run_globals() {
    // Collect file-scope defines that need evaluation
    std::vector<std::tuple<int, std::string, std::optional<SourceRange>>> need_evaluation;
    for (auto const & [qname, symbol] : tables.symbols) {
        if (std::holds_alternative<DefineSymbol>(symbol)) {
            auto const & defn = std::get<DefineSymbol>(symbol);
            auto [fid, obj, name] = parse_scope(qname);
            if (!obj) {
                auto def_it = defines.find(name);
                if (def_it != defines.end()) {
                    auto sfid = std::to_string(fid);
                    auto & context = tables.contexts[sfid];
                    context[name] = def_it->second;
                } else {
                    need_evaluation.emplace_back(fid, name, defn.node.location);
                }
            }
        }
    }

    // Create evaluator (persists for later stages)
    evaluator = std::make_unique<Evaluator>(diagnostics, tables);

    for (auto [fid, name, loc] : need_evaluation) {
        auto sfid = std::to_string(fid);
        auto & context = tables.contexts[sfid];
        try {
            evaluator->retrieve_value(sfid, name, context, loc);
        } catch (CompileError const & e) {
            emit_error(e.message, e.location);
        }
    }
    if (report_errors()) return 3;

#if !defined(NDEBUG)
    std::cerr << "After evaluating contexts (#3):" << std::endl;
    tables.dump_contexts(std::cerr);
#endif

    return std::nullopt;
}

} // namespace autocog::compiler::stl
