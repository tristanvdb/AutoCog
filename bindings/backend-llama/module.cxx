
#include "autocog/backend/llama/model.hxx"
#include "autocog/backend/llama/evaluation.hxx"
#include "autocog/backend/llama/manager.hxx"
#include "autocog/backend/llama/prepared.hxx"

#include "autocog/codec/json.hxx"
#include "autocog/data/store.hxx"

#include "autocog/build_info.hxx"

#include <pybind11/pybind11.h>
#include "errors.hxx"
#include <pybind11/stl.h>

#include <functional>
#include <map>
#include <memory>
#include <optional>

namespace py    = pybind11;
namespace data  = autocog::data;
namespace codec = autocog::codec;

PYBIND11_MODULE(backend_llama_cxx, module) {
    using namespace autocog::backend::llama;

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

    module.def("set_seed",
        [](ModelID model, unsigned seed) {
            Manager::get_model(model).set_seed(seed);
        },
        "Set the RNG seed for a model",
        py::arg("model"),
        py::arg("seed")
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
        [](ModelID model, std::string const & fta_id) -> std::string {
            // Evaluate the stored FTA, detokenize the resulting FTT with the
            // model (only possible here, where it is loaded), and hand it to the
            // store. The Manager's working evaluation is a transient: once the
            // FTT is materialized we release it (resuming is not implemented).
            auto const & fta = data::datastore().fta.get(fta_id);
            EvalID eval_id = Manager::add_eval(model, fta);
            Manager::advance(eval_id, std::nullopt);
            data::FTT ftt = Manager::retrieve(eval_id);
            detokenize(model, ftt);
            Manager::rm_eval(eval_id);

            // The FTT is a derived artifact: it inherits the FTA's provenance
            // (sta/syntax/search), adds the FTA itself, and records the
            // evaluating model by its full GGUF hash. datastore().ftt.add then
            // finalizes it, keying the store on its content hash.
            ftt.provenance = fta.provenance;
            ftt.provenance["fta"]   = fta.metadata ? fta.metadata->hash : std::string{};
            ftt.provenance["model"] = Manager::get_model(model).sha256();

            return data::datastore().ftt.add(std::make_unique<data::FTT>(std::move(ftt)));
        },
        "Evaluate a stored FTA (by handle) with a model; detokenizes the FTT and "
        "stores it, returning its handle. Read it back via the runtime-sta FTT "
        "verbs (get_ftt / dump_ftt / walk_ftt_to_frame), which need no model.",
        py::arg("model"),
        py::arg("fta_id")
    );
}
