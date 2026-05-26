
#include "autocog/runtime/sta/instantiate.hxx"
#include "autocog/runtime/sta/load.hxx"
#include "autocog/runtime/sta/parse.hxx"
#include "autocog/runtime/sta/syntax.hxx"
#include "autocog/runtime/store/store.hxx"

#include <fstream>
#include <pybind11/pybind11.h>
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

// Convert FieldValue to py::object
static py::object field_value_to_py(sta::FieldValue const & val) {
    if (val.is_string()) return py::str(val.as_string());
    if (val.is_record()) {
        py::dict d;
        for (auto const & [key, v] : val.as_record()) {
            d[py::str(key)] = field_value_to_py(v);
        }
        return d;
    }
    if (val.is_array()) {
        py::list l;
        for (auto const & v : val.as_array()) {
            l.append(field_value_to_py(v));
        }
        return l;
    }
    return py::none();
}

PYBIND11_MODULE(runtime_sta_cxx, module) {
    module.doc() = "AutoCog STA runtime — instantiation and parsing";

    module.def("load_syntax",
        [](std::string const & path) -> int {
            return store::syntaxes().add(sta::load_syntax(path));
        },
        "Load a syntax description from a JSON file and return a SyntaxID",
        py::arg("path")
    );

    module.def("load_program",
        [](std::string const & path) -> int {
            std::ifstream f(path);
            if (!f.is_open()) {
                throw std::runtime_error("Cannot open STA file: " + path);
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
           py::dict content, int syntax_id) -> int {

            auto const & program = store::programs().get(program_id);
            auto const & syntax = store::syntaxes().get(syntax_id);

            auto it = program.prompts.find(prompt_name);
            if (it == program.prompts.end()) {
                throw std::runtime_error("Prompt '" + prompt_name + "' not found in program");
            }

            auto content_json = py_to_json(content);
            auto fta_json = sta::instantiate(it->second, content_json, syntax);
            return store::ftas().add(std::move(fta_json));
        },
        "Instantiate an STA prompt into a text-level FTA, return FTAID",
        py::arg("program_id"),
        py::arg("prompt"),
        py::arg("content") = py::dict(),
        py::arg("syntax_id") = 0
    );

    module.def("parse_text",
        [](int program_id, std::string const & prompt_name,
           int syntax_id, std::string const & text,
           py::object content_obj) -> py::dict {

            auto const & program = store::programs().get(program_id);
            auto const & syntax = store::syntaxes().get(syntax_id);

            auto it = program.prompts.find(prompt_name);
            if (it == program.prompts.end()) {
                throw std::runtime_error("Prompt '" + prompt_name + "' not found in program");
            }

            sta::FieldRecord result;
            if (!content_obj.is_none()) {
                nlohmann::json content = py_to_json(content_obj);
                result = sta::parse_text(it->second, syntax, text, &content);
            } else {
                result = sta::parse_text(it->second, syntax, text);
            }
            py::dict d;
            for (auto const & [key, val] : result) {
                d[py::str(key)] = field_value_to_py(val);
            }
            return d;
        },
        "Parse STA-formatted text back to a field dict",
        py::arg("program_id"),
        py::arg("prompt"),
        py::arg("syntax_id"),
        py::arg("text"),
        py::arg("content") = py::none()
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

    module.def("release_syntax",
        [](int syntax_id) {
            store::syntaxes().remove(syntax_id);
        },
        "Release a syntax",
        py::arg("syntax_id")
    );
}
