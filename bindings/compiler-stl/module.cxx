
#include "autocog/compiler/stl/driver.hxx"
#include "autocog/data/store.hxx"

#include <pybind11/pybind11.h>
#include "errors.hxx"
#include <pybind11/stl.h>

#include <nlohmann/json.hpp>
#include <map>
#include <mutex>
#include <sstream>

namespace py = pybind11;

namespace {

// Diagnostics are not a tracked artifact (the datastore only holds finalized
// STA/FTA/FTT), so they live in a module-local side table keyed by the program
// handle (the STA's content hash). Guarded because the bindings are driven from
// multiple threads.
std::map<std::string, nlohmann::json> & diag_table() {
    static std::map<std::string, nlohmann::json> table;
    return table;
}
std::mutex & diag_mutex() { static std::mutex m; return m; }

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
    using autocog::data::DiagnosticLevel;
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
           std::vector<std::string> entry_points) -> std::string {

            using namespace autocog::compiler::stl;

            Driver driver;
            driver.print_diagnostics = false;
            driver.inputs.push_back(filepath);
            for (auto & inc : includes) driver.includes.push_back(std::move(inc));
            driver.entry_points.clear();
            for (auto & ep : entry_points) driver.entry_points.push_back(std::move(ep));

            try {
                driver.compile();
            } catch (autocog::CompileError const & e) {
                throw autocog::utilities::InternalError(
                    std::string("CompileError escaped compile(): ") + e.what());
            }

            // Always mint a handle (even on failure: the empty program is
            // harmless and the caller releases it after raising). The compiler
            // has already finalized the STA, so the store keys on its content
            // hash; diagnostics are filed under that same handle.
            nlohmann::json diags = diagnostics_to_json(driver);
            auto sta = std::make_unique<autocog::data::STA>(std::move(driver.sta));
            std::string pid = autocog::data::datastore().sta.add(std::move(sta));
            {
                std::lock_guard<std::mutex> lock(diag_mutex());
                diag_table()[pid] = std::move(diags);
            }
            return pid;
        },
        "Compile an STL file and return a program handle",
        py::arg("filepath"),
        py::arg("includes") = std::vector<std::string>{},
        py::arg("entry_points") = std::vector<std::string>{"main"}
    );

    module.def("get_diagnostics",
        [](std::string const & program_id) -> py::object {
            nlohmann::json arr = nlohmann::json::array();
            {
                std::lock_guard<std::mutex> lock(diag_mutex());
                auto it = diag_table().find(program_id);
                if (it != diag_table().end()) arr = it->second;
            }
            // Diagnostics are not a tracked structure; translate via json.
            auto json_module = py::module_::import("json");
            return json_module.attr("loads")(arr.dump());
        },
        "Retrieve the compile diagnostics for a program handle as a list of dicts",
        py::arg("program_id")
    );

    module.def("release",
        [](std::string const & program_id) {
            autocog::data::datastore().sta.release(program_id);
            std::lock_guard<std::mutex> lock(diag_mutex());
            diag_table().erase(program_id);
        },
        "Release a compiled program and its diagnostics",
        py::arg("program_id")
    );

}
