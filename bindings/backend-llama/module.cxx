
#include "autocog/runtime/fta/fta.hxx"
#include "autocog/runtime/fta/ftt.hxx"

#include "autocog/backend/llama/model.hxx"
#include "autocog/backend/llama/evaluation.hxx"
#include "autocog/backend/llama/manager.hxx"

#include "autocog/runtime/store/store.hxx"

#include "autocog/backend/llama/convert.hxx"

#include "autocog/build_info.hxx"

#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

#include <functional>
#include <optional>

namespace py = pybind11;
namespace store = autocog::runtime::store;

PYBIND11_MODULE(backend_llama_cxx, module) {
    using namespace autocog::backend::llama;
    using namespace autocog::runtime::fta;

    Manager::initialize();

    module.doc() = "AutoCog llama.cpp backend";

    module.def("build_info", &autocog::build_info, "Build configuration info");

    module.def("create",
        [](std::string const & model_path, int n_ctx) {
            return Manager::add_model(model_path, n_ctx);
        },
        "Load a GGUF model and return a ModelID",
        py::arg("model_path"),
        py::arg("n_ctx") = 4096
    );

    module.def("vocab_size", [](ModelID model) {
        return Manager::get_model(model).vocab_size();
    }, "Get vocabulary size");

    module.def("tokenize",
        [](ModelID model, std::string const & text, bool add_bos, bool special) {
            auto tokens = Manager::get_model(model).tokenize(text, add_bos, special);
            py::list result;
            for (auto token : tokens) result.append(token);
            return result;
        },
        "Tokenize text",
        py::arg("model"),
        py::arg("text"),
        py::arg("add_bos") = false,
        py::arg("special") = false
    );

    module.def("detokenize",
        [](ModelID model, py::list const & py_tokens, bool spec_rm, bool spec_unp) {
            TokenSequence tokens;
            for (auto item : py_tokens) tokens.push_back(item.cast<TokenID>());
            return Manager::get_model(model).detokenize(tokens, spec_rm, spec_unp);
        },
        "Detokenize tokens to text",
        py::arg("model"),
        py::arg("tokens"),
        py::arg("spec_rm") = false,
        py::arg("spec_unp") = false
    );

    module.def("evaluate",
        [](ModelID model, int fta_id) -> EvalID {
            auto const & fta_json = store::ftas().get(fta_id);
            FTA fta = convert_json_to_fta(model, fta_json);
            EvalID eval_id = Manager::add_eval(model, fta);
            Manager::advance(eval_id, std::nullopt);
            return eval_id;
        },
        "Evaluate a text-level FTA (by ID) using a model, return FTTID (EvalID)",
        py::arg("model"),
        py::arg("fta_id")
    );

    module.def("evaluate_json",
        [](ModelID model, std::string const & fta_json_str) -> EvalID {
            auto fta_json = nlohmann::json::parse(fta_json_str);
            FTA fta = convert_json_to_fta(model, fta_json);
            EvalID eval_id = Manager::add_eval(model, fta);
            Manager::advance(eval_id, std::nullopt);
            return eval_id;
        },
        "Evaluate an FTA from JSON string, return FTTID (EvalID)",
        py::arg("model"),
        py::arg("fta_json")
    );

    module.def("get_best",
        [](ModelID model, EvalID eval_id, int n) -> py::list {
            FTT const & ftt = Manager::retrieve(eval_id);
            Model & mdl = Manager::get_model(model);

            // Extract best paths
            std::function<void(FTT const &, std::vector<TokenID>, std::vector<std::vector<TokenID>> &)> collect =
                [&](FTT const & node, std::vector<TokenID> path, std::vector<std::vector<TokenID>> & paths) {
                    path.insert(path.end(), node.tokens.begin(), node.tokens.end());
                    bool has_children = false;
                    for (auto const & child : node.get_children()) {
                        if (!child.pruned) {
                            has_children = true;
                            collect(child, path, paths);
                        }
                    }
                    if (!has_children) {
                        paths.push_back(path);
                    }
                };

            std::vector<std::vector<TokenID>> token_paths;
            collect(ftt, {}, token_paths);

            py::list result;
            for (int i = 0; i < std::min(n, static_cast<int>(token_paths.size())); ++i) {
                auto text = mdl.detokenize(token_paths[i], false, false);
                // Replace invalid UTF-8 bytes with ? for safety
                std::string safe;
                safe.reserve(text.size());
                for (unsigned char c : text) {
                    safe.push_back(c < 0x80 ? c : '?');
                }
                result.append(safe);
            }
            return result;
        },
        "Get the n best path texts from an FTT",
        py::arg("model"),
        py::arg("ftt_id"),
        py::arg("n") = 1
    );

    module.def("release_ftt",
        [](EvalID eval_id) {
            Manager::rm_eval(eval_id);
        },
        "Release an FTT evaluation",
        py::arg("ftt_id")
    );

    module.def("get_ftt_json",
        [](ModelID model_id, EvalID eval_id) -> std::string {
            FTT const & ftt = Manager::retrieve(eval_id);
            Model & model = Manager::get_model(model_id);
            std::function<nlohmann::json(FTT const &)> serialize =
                [&](FTT const & node) -> nlohmann::json {
                    nlohmann::json j;
                    j["action"] = node.action;
                    j["logprob"] = node.logprob;
                    j["length"] = node.length;
                    j["pruned"] = node.pruned;
                    j["text"] = model.detokenize(node.tokens, false, false);
                    nlohmann::json lps = nlohmann::json::array();
                    for (auto lp : node.logprobs) lps.push_back(lp);
                    j["logprobs"] = lps;
                    nlohmann::json kids = nlohmann::json::array();
                    for (auto const & child : node.get_children()) {
                        kids.push_back(serialize(child));
                    }
                    j["children"] = kids;
                    return j;
                };
            return serialize(ftt).dump();
        },
        "Get FTT as JSON string",
        py::arg("model_id"),
        py::arg("ftt_id")
    );
}
