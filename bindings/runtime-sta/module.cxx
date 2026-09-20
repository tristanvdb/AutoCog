
#include "autocog/runtime/sta/instantiate.hxx"
#include "autocog/runtime/sta/walk.hxx"
#include "autocog/runtime/fta/simplify.hxx"
#include "autocog/codec/json.hxx"      // load/store/dump: file + JSON-string boundary
#include "autocog/codec/python.hxx"    // read/get: direct data:: <-> py::object
#include "autocog/data/store.hxx"
#include "autocog/utilities/types.hxx"
#include "autocog/logging.hxx"
#include "autocog/python_sink.hxx"

#include <pybind11/pybind11.h>
#include "errors.hxx"
#include <pybind11/stl.h>

#include <memory>
#include <string>

namespace py    = pybind11;
namespace sta   = autocog::runtime::sta;
namespace rfta  = autocog::runtime::fta;
namespace data  = autocog::data;
namespace codec = autocog::codec;

namespace {

// Build the boundary content type straight from a Python object (no JSON hop).
autocog::types::Document to_document(py::object obj) {
    autocog::types::Document doc{autocog::types::Document::Object{}};
    codec::from_py(obj, doc);
    return doc;
}

}

PYBIND11_MODULE(runtime_sta_cxx, module) {
    module.doc() = "AutoCog STA runtime — instantiation and parsing";

    autocog::binding::register_exception_translator();
    // Install the Python bridge sink so C++ logs route to Python's logging
    {
        auto sink = std::make_shared<autocog::python_logging_sink_mt>();
        autocog::init_logger(sink);  // uses default_level
    }

    module.def("set_log_level",
        [](int level) {
            spdlog::level::level_enum lvl;
            if (level <= 5)       lvl = spdlog::level::trace;
            else if (level <= 10) lvl = spdlog::level::debug;
            else if (level <= 20) lvl = spdlog::level::info;
            else if (level <= 30) lvl = spdlog::level::warn;
            else if (level <= 40) lvl = spdlog::level::err;
            else                  lvl = spdlog::level::critical;
            autocog::set_level(lvl);
        },
        "Set C++ log level (Python logging level values: 5=TRACE..40=ERROR)",
        py::arg("level"));

    // Every tracked structure exposes the same six verbs. The file/string doors
    // (load/store/dump) are JSON via autocog::codec (disk format); the Python
    // doors (read/get) convert data:: <-> py::object directly via codec::from_py
    // / to_py, with no intermediate JSON DOM.
    //   load_<x>(path)   : JSON file   -> datastore (handle)
    //   read_<x>(obj)    : py object   -> datastore (handle)
    //   get_<x>(id)      : datastore   -> py object
    //   store_<x>(id,p)  : datastore   -> JSON file
    //   dump_<x>(id)     : datastore   -> JSON string
    //   release_<x>(id)  : drop a holder in the datastore

#define AUTOCOG_BIND_STORE(VERB_TAG, REG, TYPE, LABEL)                                       \
    module.def("load_" VERB_TAG,                                                             \
        [](std::string const & path) -> std::string {                                        \
            return data::datastore().REG.add(codec::from_file<data::TYPE>(path));            \
        }, "Load a " LABEL " from a JSON file; returns a handle", py::arg("path"));          \
    module.def("read_" VERB_TAG,                                                             \
        [](py::object obj) -> std::string {                                                  \
            if (py::isinstance<py::str>(obj))                                                \
                return data::datastore().REG.add(                                            \
                    codec::from_string<data::TYPE>(obj.cast<std::string>()));                \
            return data::datastore().REG.add(codec::from_py<data::TYPE>(obj));               \
        }, "Read a " LABEL " from a Python object or JSON string; returns a handle",          \
        py::arg("data"));                                                                    \
    module.def("get_" VERB_TAG,                                                              \
        [](std::string const & id) -> py::object {                                           \
            return codec::to_py(data::datastore().REG.get(id));                              \
        }, "Convert a stored " LABEL " to a Python object", py::arg("id"));                  \
    module.def("store_" VERB_TAG,                                                            \
        [](std::string const & id, std::string const & path) {                              \
            codec::to_file(data::datastore().REG.get(id), path);                            \
        }, "Serialize a stored " LABEL " to a JSON file", py::arg("id"), py::arg("path"));   \
    module.def("dump_" VERB_TAG,                                                             \
        [](std::string const & id) -> std::string {                                          \
            return codec::to_json(data::datastore().REG.get(id)).dump(2);                    \
        }, "Serialize a stored " LABEL " to a JSON string", py::arg("id"));                  \
    module.def("release_" VERB_TAG,                                                          \
        [](std::string const & id) { data::datastore().REG.release(id); },                  \
        "Release a " LABEL, py::arg("id"))

    AUTOCOG_BIND_STORE("syntax", syntax, Syntax,       "syntax config");
    AUTOCOG_BIND_STORE("search", search, SearchConfig, "search config");
    AUTOCOG_BIND_STORE("sta",    sta,    STA,          "pre-compiled STA");
    AUTOCOG_BIND_STORE("fta",    fta,    FTA,          "FTA");
    AUTOCOG_BIND_STORE("ftt",    ftt,    FTT,          "FTT");

#undef AUTOCOG_BIND_STORE

    // ===== evaluate an expression =====
    // The Python side resolves call arguments, and an argument may be an
    // expression that only a running engine can finish -- `zero_index use
    // __syntax__.prompt_zero_index`, whose value is a property of the run.
    // The expression semantics are not reimplemented here: this is a door onto
    // the same evaluator the compiler and the instantiator use.
    module.def("eval_expr",
        [](py::object expr, py::dict bindings) -> py::object {
            data::Expr e;
            codec::from_py(expr, e);
            auto const v = data::evaluate(e, [&bindings](std::string const & ref)
                    -> std::optional<autocog::types::Value> {
                auto const dot = ref.find('.');
                if (dot == std::string::npos) return std::nullopt;
                auto const ns = ref.substr(0, dot);
                auto const field = ref.substr(dot + 1);
                if (!bindings.contains(py::str(ns))) return std::nullopt;
                py::object table = bindings[py::str(ns)];
                if (!py::isinstance<py::dict>(table)) return std::nullopt;
                py::dict d = table.cast<py::dict>();
                if (!d.contains(py::str(field)))
                    throw autocog::ConfigError(
                        "'" + ref + "': " + ns + " has no field '" + field + "'", ref);
                data::Expr bound;
                codec::from_py(d[py::str(field)], bound);
                return bound.as_value();
            });
            return codec::to_py(data::Expr::lit(v));
        },
        "Evaluate an expression against a {namespace: {field: value}} table.\n\n"
        "An expression that is already a literal is returned as it is; a "
        "reference to a namespace the table does not carry is an error naming "
        "the reference, never a silent None.",
        py::arg("expr"), py::arg("bindings"));

    // ===== instantiate (STA -> FTA) =====
    module.def("instantiate",
        [](std::string const & program_id, std::string const & prompt_name,
           py::object content, std::string const & syntax_id,
           std::string const & search_id,
           std::string const & simplify) -> std::string {
            auto const & program = data::datastore().sta.get(program_id);
            auto const & syntax  = data::datastore().syntax.get(syntax_id);
            auto const & search  = data::datastore().search.get(search_id);

            auto it = program.prompts.find(prompt_name);
            if (it == program.prompts.end())
                throw autocog::ConfigError(
                    "Prompt '" + prompt_name + "' not found in program", prompt_name);

            auto content_doc = to_document(content);
            std::string sta_uid = program.metadata ? program.metadata->hash : std::string{};
            auto fta = sta::instantiate(it->second, content_doc, syntax, search, sta_uid);
            auto mode = rfta::parse_simplification(simplify);
            if (mode != rfta::Simplification::None) fta = rfta::simplify(fta, mode);
            return data::datastore().fta.add(std::make_unique<data::FTA>(std::move(fta)));
        },
        "Instantiate an STA prompt into an FTA; returns a handle.\n\n"
        "simplify ('none' | 'limited' | 'full') collapses a fully-determined "
        "FTA — one whose content was a complete frame — for finetuning export. "
        "It throws if anything is left to generate, and the result cannot be "
        "scored: merging text re-tokenizes across the seams.",
        py::arg("program_id"), py::arg("prompt"), py::arg("content"),
        py::arg("syntax_id"), py::arg("search_id"),
        py::arg("simplify") = "none");

    // ===== FTT -> frame =====
    module.def("walk_ftt_to_frame",
        [](std::string const & program_id, std::string const & prompt_name,
           std::string const & ftt_id, py::object content) -> py::object {
            auto const & program = data::datastore().sta.get(program_id);
            auto const & ftt     = data::datastore().ftt.get(ftt_id);
            auto content_doc = content.is_none() ? autocog::types::Document{}
                                                 : to_document(content);
            auto frame = sta::walk_ftt_to_frame(ftt, program, prompt_name,
                                                "best", content_doc);
            return codec::to_py(frame);
        },
        "Walk a stored FTT (by handle) into the prompt's frame; returns the frame dict. "
        "Field values are verbatim: a select field holds the emitted index.\n\n"
        "content seeds the frame with the document the prompt was instantiated "
        "from. A simplified FTA has no node for a field its content supplied, so "
        "those fields would otherwise be absent; the FTT overwrites whatever it "
        "recorded. Harmless on an unsimplified FTT, where every field has a node.",
        py::arg("program_id"), py::arg("prompt"), py::arg("ftt_id"),
        py::arg("content") = py::none());
}
