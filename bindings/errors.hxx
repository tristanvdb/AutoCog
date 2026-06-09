#ifndef AUTOCOG_BINDING_ERRORS_HXX
#define AUTOCOG_BINDING_ERRORS_HXX

#include "autocog/utilities/errors.hxx"
#include "autocog/utilities/exception.hxx"

#include <pybind11/pybind11.h>

namespace py = pybind11;

namespace autocog::binding {

// The `autocog.errors` module, imported once and cached. Importing it runs
// autocog/__init__.py, which re-imports the binding .so (under its package
// name) and cascades through the whole package. That MUST NOT happen while a
// translation is in flight: the re-import re-enters pybind (re-registering
// translators, reloading the module) and corrupts every subsequent
// translation -- the symptom is the first exception translating correctly and
// all later ones turning into MemoryError/SystemError or segfaulting. So the
// import is primed at registration time (below) and the translator only ever
// uses this cached handle.
inline py::object & errors_module() {
    static py::object mod = py::module_::import("autocog.errors");
    return mod;
}

inline void register_exception_translator() {
    // Register exactly once per process: the binding .so is imported under two
    // module names (e.g. `runtime_sta_cxx` standalone and
    // `autocog.runtime.sta.runtime_sta_cxx` via the package), so PYBIND11_MODULE
    // init -- and this function -- runs twice.
    static bool registered = false;
    if (registered) return;
    registered = true;

    // Prime the autocog.errors import now, outside any exception translation.
    errors_module();

    py::register_exception_translator([](std::exception_ptr p) {
        py::object errors = errors_module();

        try {
            if (p) std::rethrow_exception(p);
        }
        // Note: compiler::stl::CompileError does NOT cross the boundary. STL
        // compile errors are collected as diagnostics and surfaced by the
        // Python layer (which raises autocog.errors.CompileError itself). A
        // CompileError reaching here would be a bug; the compile binding turns
        // that into an InternalError before it ever gets to this translator.
        // Execution tree (most specific first)
        catch (FlowInvariantError const & e) {
            py::object cls = errors.attr("FlowInvariantError");
            py::object exc = cls(e.message,
                py::arg("prompt") = e.prompt,
                py::arg("field") = e.field);
            PyErr_SetObject(cls.ptr(), exc.ptr());
        }
        catch (OrchestrationError const & e) {
            py::object cls = errors.attr("OrchestrationError");
            py::object exc = cls(e.message,
                py::arg("prompt") = e.prompt,
                py::arg("field") = e.field);
            PyErr_SetObject(cls.ptr(), exc.ptr());
        }
        catch (ConfigError const & e) {
            py::object cls = errors.attr("ConfigError");
            py::object exc = cls(e.message,
                py::arg("path") = e.path);
            PyErr_SetObject(cls.ptr(), exc.ptr());
        }
        catch (SchemaError const & e) {
            py::object cls = errors.attr("SchemaError");
            py::object exc = cls(e.message,
                py::arg("path") = e.path);
            PyErr_SetObject(cls.ptr(), exc.ptr());
        }
        catch (IntegrityError const & e) {
            py::object cls = errors.attr("IntegrityError");
            py::object exc = cls(e.message,
                py::arg("format") = e.format,
                py::arg("expected") = e.expected,
                py::arg("actual") = e.actual);
            PyErr_SetObject(cls.ptr(), exc.ptr());
        }
        catch (ModelError const & e) {
            py::object cls = errors.attr("ModelError");
            py::object exc = cls(e.message,
                py::arg("model_id") = e.model_id,
                py::arg("op") = e.op);
            PyErr_SetObject(cls.ptr(), exc.ptr());
        }
        catch (FileError const & e) {
            py::object cls = errors.attr("FileError");
            py::object exc = cls(e.message,
                py::arg("path") = e.path);
            PyErr_SetObject(cls.ptr(), exc.ptr());
        }
        catch (NotImplementedError const & e) {
            py::object cls = errors.attr("NotImplementedError");
            py::object exc = cls(e.message);
            PyErr_SetObject(cls.ptr(), exc.ptr());
        }
        // Internal
        catch (utilities::InternalError const & e) {
            py::object cls = errors.attr("InternalError");
            py::object exc = cls(e.message);
            PyErr_SetObject(cls.ptr(), exc.ptr());
        }
        // Catch-all for the AutoCogError tree
        catch (AutoCogError const & e) {
            py::object cls = errors.attr("AutoCogError");
            py::object exc = cls(e.message);
            PyErr_SetObject(cls.ptr(), exc.ptr());
        }
        // Let std::exception fall through to pybind11 default
    });
}

}

#endif // AUTOCOG_BINDING_ERRORS_HXX
