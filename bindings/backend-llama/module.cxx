
#include "autocog/backend/llama/model.hxx"
#include "autocog/backend/llama/evaluation.hxx"
#include "autocog/backend/llama/manager.hxx"
#include "autocog/backend/llama/prepared.hxx"
#include "autocog/backend/llama/perf-fields.hxx"

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

    // Self-guarded: registers process-wide on the first module to load, so the
    // typed AutoCog error hierarchy survives this module being imported in
    // isolation (not only behind runtime_sta_cxx).
    autocog::binding::register_exception_translator();

    module.def("build_info", &autocog::build_info, "Build configuration info");

    module.def("create",
        [](std::string const & model_path, int n_ctx, int ngl, int kv_slots) {
            return Manager::add_model(model_path, n_ctx, ngl, kv_slots);
        },
        "Load a GGUF model and return a ModelID. ngl/kv_slots: -1 = fall "
        "back to AUTOCOG_NGL / AUTOCOG_KV_SLOTS (per-model values let a "
        "multi-model worker load each model with its own parameters)",
        py::arg("model_path"),
        py::arg("n_ctx") = 4096,
        py::arg("ngl") = -1,
        py::arg("kv_slots") = -1
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
        [](ModelID model, std::string const & fta_id) -> py::tuple {
            // Evaluate the stored FTA, detokenize the resulting FTT with the
            // model (only possible here, where it is loaded), and hand it to the
            // store. The Manager's working evaluation is a transient: once the
            // FTT is materialized we release it (resuming is not implemented).
            auto const & fta = data::datastore().fta.get(fta_id);
            // Model-level stats accumulate across evaluations on a shared
            // model: snapshot here so the returned perf holds per-eval deltas.
            DecodeStats const ds_base = Manager::get_model(model).decode_stats();
            KvStats const kv_base = Manager::get_model(model).kv_stats();
            EvalID eval_id = Manager::add_eval(model, fta);
            Manager::advance(eval_id, std::nullopt);
            std::string const perf_json = perf_fields(
                Manager::get_model(model), Manager::get_eval(eval_id),
                ds_base, kv_base).dump();
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

            auto handle = data::datastore().ftt.add(std::make_unique<data::FTT>(std::move(ftt)));
            return py::make_tuple(handle, perf_json);
        },
        "Evaluate a stored FTA (by handle) with a model; detokenizes the FTT and "
        "stores it. Returns (ftt_handle, perf_json): the same autocog.perf.* "
        "field map xfta --perf emits, as per-evaluation deltas. Read the FTT "
        "back via the runtime-sta FTT verbs (get_ftt / dump_ftt / "
        "walk_ftt_to_frame), which need no model.",
        py::arg("model"),
        py::arg("fta_id")
    );

    module.def("score",
        [](ModelID model, std::string const & ftt_id) -> std::string {
            // Score a stored (typically encoder-produced) FTT: every node's
            // logprobs become P(token | prefix) under this model — the efta
            // --score semantics. The input artifact is immutable in the store,
            // so the scored tree is stored as a new artifact.
            data::FTT ftt = data::datastore().ftt.get(ftt_id);   // copy
            // Encoder output is text-level; tokenization needs the model
            // (same pipeline as efta: encode -> tokenize -> score).
            autocog::backend::llama::tokenize(model, ftt);
            autocog::backend::llama::score(model, ftt);
            detokenize(model, ftt);
            ftt.provenance["model"] = Manager::get_model(model).sha256();
            ftt.metadata.reset();   // scored content differs: re-finalize fresh
            return data::datastore().ftt.add(std::make_unique<data::FTT>(std::move(ftt)));
        },
        "Score a stored FTT against the model (forced P(token|prefix) on every "
        "node, the efta --score semantics); returns the scored FTT's handle.",
        py::arg("model"),
        py::arg("ftt_id")
    );

    module.def("reset",
        [](ModelID model, bool kv) {
            Manager::get_model(model).reset_stats();
            if (kv) Manager::get_model(model).clear_kv();
        },
        "Zero the model's accumulated kv/decode counters; with kv=True also "
        "drop every KV slot (cold-cache isolation between measured runs).",
        py::arg("model"),
        py::arg("kv") = true
    );
}
