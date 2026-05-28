
#include "autocog/runtime/sta/instantiate.hxx"
#include "autocog/runtime/sta/load.hxx"
#include "autocog/runtime/sta/syntax.hxx"
#include "autocog/runtime/store/store.hxx"
#include "autocog/logging.hxx"
#include "autocog/python_sink.hxx"

#include <fstream>
#include <pybind11/pybind11.h>
#include "errors.hxx"
#include <pybind11/stl.h>

namespace py = pybind11;
namespace sta = autocog::runtime::sta;
namespace store = autocog::runtime::store;

// Convert py::object to nlohmann::json
static nlohmann::json py_to_json(py::handle obj) {
    if (obj.is_none()) return nullptr;
    if (py::isinstance<py::bool_>(obj)) return obj.cast<bool>();
    if (py::isinstance<py::int_>(obj)) return obj.cast<int>();
    if (py::isinstance<py::float_>(obj)) return obj.cast<double>();
    if (py::isinstance<py::str>(obj)) return obj.cast<std::string>();
    if (py::isinstance<py::list>(obj)) {
        nlohmann::json arr = nlohmann::json::array();
        for (auto item : obj) arr.push_back(py_to_json(item));
        return arr;
    }
    if (py::isinstance<py::dict>(obj)) {
        nlohmann::json dict = nlohmann::json::object();
        for (auto item : obj.cast<py::dict>()) {
            dict[item.first.cast<std::string>()] = py_to_json(item.second);
        }
        return dict;
    }
    return nullptr;
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
            // Map Python logging levels to spdlog levels
            spdlog::level::level_enum lvl;
            if (level <= 5)       lvl = spdlog::level::trace;
            else if (level <= 10) lvl = spdlog::level::debug;
            else if (level <= 20) lvl = spdlog::level::info;
            else if (level <= 30) lvl = spdlog::level::warn;
            else if (level <= 40) lvl = spdlog::level::err;
            else                  lvl = spdlog::level::critical;
            autocog::set_level(lvl);
        },
        "Set C++ log level (use Python logging level values: 5=TRACE, 10=DEBUG, 20=INFO, 30=WARNING, 40=ERROR)",
        py::arg("level")
    );

    module.def("load_syntax",
        [](std::string const & path) -> int {
            return store::syntaxes().add(sta::load_syntax(path));
        },
        "Load a syntax description from a JSON file and return a SyntaxID",
        py::arg("path")
    );

    module.def("load_search_config",
        [](std::string const & path) -> int {
            return store::search_configs().add(sta::load_search_config(path));
        },
        "Load a search config from a JSON file and return a SearchConfigID",
        py::arg("path")
    );

    module.def("load_program",
        [](std::string const & path) -> int {
            std::ifstream f(path);
            if (!f.is_open()) {
                throw autocog::FileError("Cannot open STA file: " + path, path);
            }
            auto j = nlohmann::json::parse(f);
            auto program = sta::load_program(j);
            return store::programs().add(std::move(program));
        },
        "Load a pre-compiled STA program from a JSON file and return a ProgramID",
        py::arg("path")
    );

    module.def("instantiate",
        [](int program_id, std::string const & prompt_name,
           py::dict content, int syntax_id, int search_id) -> int {

            auto const & program = store::programs().get(program_id);
            auto const & syntax = store::syntaxes().get(syntax_id);
            auto const & search = store::search_configs().get(search_id);

            auto it = program.prompts.find(prompt_name);
            if (it == program.prompts.end()) {
                throw autocog::ConfigError("Prompt '" + prompt_name + "' not found in program", prompt_name);
            }

            auto content_json = py_to_json(content);
            auto fta_json = sta::instantiate(it->second, content_json, syntax, search);
            return store::ftas().add(std::move(fta_json));
        },
        "Instantiate an STA prompt into a text-level FTA, return FTAID",
        py::arg("program_id"),
        py::arg("prompt"),
        py::arg("content"),
        py::arg("syntax_id"),
        py::arg("search_id")
    );

    module.def("release_fta",
        [](int fta_id) {
            store::ftas().remove(fta_id);
        },
        "Release an FTA",
        py::arg("fta_id")
    );

    module.def("get_fta_json",
        [](int fta_id) -> std::string {
            auto const & fta = store::ftas().get(fta_id);
            return fta.dump();
        },
        "Get FTA as JSON string",
        py::arg("fta_id")
    );

    module.def("store_fta_json",
        [](std::string const & fta_json_str) -> int {
            auto fta = nlohmann::json::parse(fta_json_str);
            return store::ftas().add(std::move(fta));
        },
        "Store an FTA JSON string and return an FTAID",
        py::arg("fta_json")
    );

    module.def("release_syntax",
        [](int syntax_id) {
            store::syntaxes().remove(syntax_id);
        },
        "Release a syntax",
        py::arg("syntax_id")
    );
}
