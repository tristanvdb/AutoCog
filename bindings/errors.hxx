#ifndef AUTOCOG_BINDING_ERRORS_HXX
#define AUTOCOG_BINDING_ERRORS_HXX

#include "autocog/utilities/errors.hxx"
#include "autocog/utilities/exception.hxx"
#include "autocog/compiler/stl/diagnostic.hxx"

#include <pybind11/pybind11.h>

namespace py = pybind11;

namespace autocog::binding {

inline void register_exception_translator() {
    py::register_exception_translator([](std::exception_ptr p) {
        py::object errors = py::module_::import("autocog.errors");

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
                py::arg("recoverable") = e.recoverable,
                py::arg("prompt") = e.prompt,
                py::arg("field") = e.field);
            PyErr_SetObject(cls.ptr(), exc.ptr());
        }
        catch (OrchestrationError const & e) {
            py::object cls = errors.attr("OrchestrationError");
            py::object exc = cls(e.message,
                py::arg("recoverable") = e.recoverable,
                py::arg("prompt") = e.prompt,
                py::arg("field") = e.field);
            PyErr_SetObject(cls.ptr(), exc.ptr());
        }
        catch (ConfigError const & e) {
            py::object cls = errors.attr("ConfigError");
            py::object exc = cls(e.message,
                py::arg("recoverable") = e.recoverable,
                py::arg("path") = e.path);
            PyErr_SetObject(cls.ptr(), exc.ptr());
        }
        catch (ModelError const & e) {
            py::object cls = errors.attr("ModelError");
            py::object exc = cls(e.message,
                py::arg("recoverable") = e.recoverable,
                py::arg("model_id") = e.model_id,
                py::arg("op") = e.op);
            PyErr_SetObject(cls.ptr(), exc.ptr());
        }
        catch (FileError const & e) {
            py::object cls = errors.attr("FileError");
            py::object exc = cls(e.message,
                py::arg("recoverable") = e.recoverable,
                py::arg("path") = e.path);
            PyErr_SetObject(cls.ptr(), exc.ptr());
        }
        catch (NotImplementedError const & e) {
            py::object cls = errors.attr("NotImplementedError");
            py::object exc = cls(e.message,
                py::arg("recoverable") = e.recoverable);
            PyErr_SetObject(cls.ptr(), exc.ptr());
        }
        // Internal
        catch (utilities::InternalError const & e) {
            py::object cls = errors.attr("InternalError");
            py::object exc = cls(e.message,
                py::arg("recoverable") = e.recoverable);
            PyErr_SetObject(cls.ptr(), exc.ptr());
        }
        // Catch-all for the AutoCogError tree
        catch (AutoCogError const & e) {
            py::object cls = errors.attr("AutoCogError");
            py::object exc = cls(e.message,
                py::arg("recoverable") = e.recoverable);
            PyErr_SetObject(cls.ptr(), exc.ptr());
        }
        // Let std::exception fall through to pybind11 default
    });
}

}

#endif // AUTOCOG_BINDING_ERRORS_HXX
