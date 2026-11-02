
#include "autocog/backend/llama/model.hxx"
#include "autocog/backend/llama/evaluation.hxx"
#include "autocog/backend/llama/manager.hxx"
#include "autocog/backend/llama/prepared.hxx"
#include "autocog/backend/llama/perf-fields.hxx"

#include "autocog/codec/json.hxx"
#include "autocog/data/store.hxx"

#include "autocog/build_info.hxx"

#include <ggml-backend.h>

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

    module.def("build_options", []() {
        py::dict d;
        d["build_type"] = AUTOCOG_BUILD_TYPE;
        d["cuda"] = static_cast<bool>(AUTOCOG_CUDA);
        d["rocm"] = static_cast<bool>(AUTOCOG_ROCM);
        d["blas"] = static_cast<bool>(AUTOCOG_BLAS);
        d["vulkan"] = static_cast<bool>(AUTOCOG_VULKAN);
        d["metal"] = static_cast<bool>(AUTOCOG_METAL);
        d["native"] = static_cast<bool>(AUTOCOG_NATIVE);
        d["tuned"] = static_cast<bool>(AUTOCOG_TUNED);
        return d;
    }, "Build configuration as a dict (backend acceleration flags)");

    module.def("devices", []() {
        py::list out;
        for (size_t i = 0; i < ggml_backend_dev_count(); ++i) {
            ggml_backend_dev_t dev = ggml_backend_dev_get(i);
            size_t free = 0, total = 0;
            ggml_backend_dev_memory(dev, &free, &total);
            char const * type = "accel";
            switch (ggml_backend_dev_type(dev)) {
                case GGML_BACKEND_DEVICE_TYPE_CPU:   type = "cpu";  break;
                case GGML_BACKEND_DEVICE_TYPE_GPU:   type = "gpu";  break;
                case GGML_BACKEND_DEVICE_TYPE_IGPU:  type = "igpu"; break;
                case GGML_BACKEND_DEVICE_TYPE_META:  type = "meta"; break;
                default: break;
            }
            py::dict d;
            d["name"] = ggml_backend_dev_name(dev);
            d["description"] = ggml_backend_dev_description(dev);
            d["type"] = type;
            d["memory_total"] = total;
            d["memory_free"] = free;
            out.append(d);
        }
        return out;
    }, "Devices visible at runtime, whatever the vendor; a GPU build with no "
       "gpu/igpu entry here silently runs on CPU");

    module.def("create",
        [](std::string const & model_path, int n_ctx, int ngl, int kv_slots) {
            // Weight loading is seconds to minutes of pure C++ — a worker
            // hosting several models would otherwise block its own startup
            // (and any concurrent load) on the GIL.
            py::gil_scoped_release nogil;
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

    module.def("kv_slots", [](ModelID model) {
        return Manager::get_model(model).kv_slots();
    },
    "KV sequence slots this model was loaded with -- the EFFECTIVE count, "
    "after the per-model value, AUTOCOG_KV_SLOTS and the built-in default "
    "have been resolved. Slot count changes free-text sampling and shifts "
    "every logprob slightly (see tests/integration/modules/test_kv_slots.py), "
    "so it is an experimental variable and a campaign has to record it. "
    "Asking the model is the only way to learn it: the request may have said "
    "nothing at all.",
    py::arg("model"));

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
        [](ModelID model, std::string const & fta_id, bool score_all) -> py::tuple {
            // Evaluate the stored FTA, detokenize the resulting FTT with the
            // model (only possible here, where it is loaded), and hand it to the
            // store. The Manager's working evaluation is a transient: once the
            // FTT is materialized we release it (resuming is not implemented).
            //
            // The GIL is released across the evaluation itself: it is the long
            // pole (up to seconds), touches no Python object, and holding it
            // starves the whole interpreter. In a level-3 worker that means
            // the HTTP server goes dark for the duration of every evaluation
            // -- /status and /stats time out, so a monitor reports a healthy
            // busy worker as unreachable, and RequestQueue lanes
            // (max_workers > 1) cannot overlap because the threads serialize
            // on the GIL instead. Everything inside the guard is pure C++;
            // the Python tuple is built after it is dropped.
            std::string ftt_handle;
            std::string perf_json;
            {
                py::gil_scoped_release nogil;

                auto const & fta = data::datastore().fta.get(fta_id);
                // Model-level stats accumulate across evaluations on a shared
                // model: snapshot here so the returned perf holds per-eval deltas.
                DecodeStats const ds_base = Manager::get_model(model).decode_stats();
                KvStats const kv_base = Manager::get_model(model).kv_stats();
                EvaluationConfig config;
                config.score_all = score_all;
                EvalID eval_id = Manager::add_eval(model, fta, config);
                Manager::advance(eval_id, std::nullopt);
                perf_json = perf_fields(
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

                ftt_handle = data::datastore().ftt.add(
                    std::make_unique<data::FTT>(std::move(ftt)));
            }
            return py::make_tuple(ftt_handle, perf_json);
        },
        "Evaluate a stored FTA (by handle) with a model; detokenizes the FTT and "
        "stores it. Returns (ftt_handle, perf_json): the same autocog.perf.* "
        "field map xfta --perf emits, as per-evaluation deltas. Read the FTT "
        "back via the runtime-sta FTT verbs (get_ftt / dump_ftt / "
        "walk_ftt_to_frame), which need no model.\n\n"
        "score_all=True scores every text action instead of only the ones the "
        "FTA marks. On an FTA instantiated from a complete frame — one forced "
        "path — that fills P(token | prefix) for every token the model did not "
        "choose: constraint friction. It is pure cost during generation.",
        py::arg("model"),
        py::arg("fta_id"),
        py::arg("score_all") = false
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
