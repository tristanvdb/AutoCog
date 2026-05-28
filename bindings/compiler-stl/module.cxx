
#include "autocog/compiler/stl/driver.hxx"
#include "autocog/runtime/sta/load.hxx"
#include "autocog/runtime/store/store.hxx"

#include <pybind11/pybind11.h>
#include "errors.hxx"
#include <pybind11/stl.h>

#include <sstream>

namespace py = pybind11;
namespace store = autocog::runtime::store;

PYBIND11_MODULE(compiler_stl_cxx, module) {
    module.doc() = "AutoCog STL compiler";

    module.def("compile",
        [](std::string const & filepath,
           std::vector<std::string> includes,
           std::vector<std::string> entry_points) -> int {

            using namespace autocog::compiler::stl;

            Driver driver;
            driver.inputs.push_back(filepath);
            for (auto & inc : includes) driver.includes.push_back(std::move(inc));
            driver.entry_points.clear();
            for (auto & ep : entry_points) driver.entry_points.push_back(std::move(ep));

            auto err = driver.compile();
            if (err) {
                throw autocog::compiler::stl::CompileError("Compilation failed with error code " + std::to_string(*err));
            }

            return store::programs().add(std::move(driver.sta));
        },
        "Compile an STL file and return a ProgramID",
        py::arg("filepath"),
        py::arg("includes") = std::vector<std::string>{},
        py::arg("entry_points") = std::vector<std::string>{"main"}
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
        },
        "Release a compiled program",
        py::arg("program_id")
    );

}
