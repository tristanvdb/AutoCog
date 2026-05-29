
#include "autocog/compiler/stl/driver.hxx"
#include "autocog/runtime/sta/load.hxx"
#include "autocog/runtime/store/store.hxx"

#include <pybind11/pybind11.h>
#include "errors.hxx"
#include <pybind11/stl.h>

#include <nlohmann/json.hpp>
#include <sstream>

namespace py = pybind11;
namespace store = autocog::runtime::store;

namespace {

// Resolve a file id to its path using the driver's filename->id map.
std::string resolve_fid(autocog::compiler::stl::Driver const & driver, int fid) {
    for (auto const & [path, id] : driver.fileids) {
        if (id == fid) return path;
    }
    return "<unknown>";
}

// Serialize the driver's diagnostics into a JSON array, resolving each
// location's file id to a path so the Python side never sees raw ids.
nlohmann::json diagnostics_to_json(autocog::compiler::stl::Driver const & driver) {
    using autocog::compiler::stl::DiagnosticLevel;
    nlohmann::json arr = nlohmann::json::array();
    for (auto const & diag : driver.diagnostics_log()) {
        nlohmann::json rec;
        switch (diag.level) {
            case DiagnosticLevel::Error:   rec["level"] = "error";   break;
            case DiagnosticLevel::Warning: rec["level"] = "warning"; break;
            case DiagnosticLevel::Note:    rec["level"] = "note";    break;
        }
        rec["message"] = diag.message;
        if (diag.location) {
            auto const & loc = *diag.location;
            rec["location"] = {
                {"file",   resolve_fid(driver, loc.fid)},
                {"line",   loc.line},
                {"column", loc.column},
            };
        } else {
            rec["location"] = nullptr;
        }
        if (diag.source_line) rec["source_line"] = *diag.source_line;
        else                  rec["source_line"] = nullptr;
        rec["notes"] = diag.notes;
        arr.push_back(std::move(rec));
    }
    return arr;
}

}

PYBIND11_MODULE(compiler_stl_cxx, module) {
    module.doc() = "AutoCog STL compiler";

    module.def("compile",
        [](std::string const & filepath,
           std::vector<std::string> includes,
           std::vector<std::string> entry_points) -> int {

            using namespace autocog::compiler::stl;

            Driver driver;
            // Diagnostics are routed through Python logging by the caller, not
            // printed to stderr here (avoids double-logging).
            driver.print_diagnostics = false;
            driver.inputs.push_back(filepath);
            for (auto & inc : includes) driver.includes.push_back(std::move(inc));
            driver.entry_points.clear();
            for (auto & ep : entry_points) driver.entry_points.push_back(std::move(ep));

            // Compile errors are accumulated as diagnostics, not thrown. A
            // CompileError escaping here would be a compiler bug, so surface it
            // as a fatal internal error rather than letting it become a bare
            // RuntimeError at the boundary.
            try {
                driver.compile();
            } catch (CompileError const & e) {
                throw autocog::utilities::InternalError(
                    std::string("CompileError escaped compile(): ") + e.what());
            }

            // Always mint an id (even on failure: the empty program is harmless
            // and the caller releases it after raising). Store the diagnostics
            // under that same id so Python can retrieve them by program id.
            nlohmann::json diags = diagnostics_to_json(driver);
            int pid = store::programs().add(std::move(driver.sta));
            store::diagnostics().set(pid, std::move(diags));
            return pid;
        },
        "Compile an STL file and return a ProgramID",
        py::arg("filepath"),
        py::arg("includes") = std::vector<std::string>{},
        py::arg("entry_points") = std::vector<std::string>{"main"}
    );

    module.def("get_diagnostics",
        [](int program_id) -> py::object {
            nlohmann::json arr = store::diagnostics().has(program_id)
                ? store::diagnostics().get(program_id)
                : nlohmann::json::array();
            auto json_module = py::module_::import("json");
            return json_module.attr("loads")(arr.dump());
        },
        "Retrieve the compile diagnostics for a ProgramID as a list of dicts",
        py::arg("program_id")
    );

    module.def("emit",
        [](int program_id, std::string const & target) -> py::object {
            if (target != "sta") {
                throw autocog::ConfigError("Unknown emit target: " + target, target);
            }
            auto const & program = store::programs().get(program_id);
            auto j = autocog::runtime::sta::serialize_program(program);
            auto json_module = py::module_::import("json");
            return json_module.attr("loads")(j.dump());
        },
        "Emit program data as a Python dict",
        py::arg("program_id"),
        py::arg("target") = "sta"
    );

    module.def("release",
        [](int program_id) {
            store::programs().remove(program_id);
            store::diagnostics().remove(program_id);
        },
        "Release a compiled program and its diagnostics",
        py::arg("program_id")
    );

}
