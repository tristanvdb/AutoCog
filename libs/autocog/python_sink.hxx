#ifndef AUTOCOG_PYTHON_SINK_HXX
#define AUTOCOG_PYTHON_SINK_HXX

#include <spdlog/sinks/base_sink.h>
#include <pybind11/pybind11.h>
#include <mutex>
#include <string>

namespace py = pybind11;

namespace autocog {

// Custom spdlog sink that forwards log records to Python's logging module.
// The Python logger handle is cached at construction and refreshed lazily if
// it is missing (e.g. constructed before the logging module was importable),
// so the hot path avoids re-importing `logging` and re-looking-up the logger
// on every record.
template <typename Mutex>
class python_logging_sink : public spdlog::sinks::base_sink<Mutex> {
public:
    python_logging_sink() {
        try {
            py::gil_scoped_acquire gil;
            py_logger_ = py::module_::import("logging").attr("getLogger")("autocog");
        } catch (...) {
            // Couldn't acquire the GIL / import logging at construction time;
            // we retry per-call. Defensive against unusual init ordering.
        }
    }

protected:
    void sink_it_(const spdlog::details::log_msg & msg) override {
        try {
            py::gil_scoped_acquire gil;
            if (!py_logger_) {
                py_logger_ = py::module_::import("logging").attr("getLogger")("autocog");
            }
            int py_level = to_python_level(msg.level);
            std::string text(msg.payload.data(), msg.payload.size());
            py_logger_.attr("log")(py_level, text);
        } catch (...) {
            // Interpreter may be finalizing — silently drop the record
        }
    }

    void flush_() override {}

private:
    py::object py_logger_;

    static int to_python_level(spdlog::level::level_enum lvl) {
        switch (lvl) {
            case spdlog::level::trace:    return 5;   // TRACE (custom)
            case spdlog::level::debug:    return 10;  // DEBUG
            case spdlog::level::info:     return 20;  // INFO
            case spdlog::level::warn:     return 30;  // WARNING
            case spdlog::level::err:      return 40;  // ERROR
            case spdlog::level::critical: return 50;  // CRITICAL
            default:                      return 20;
        }
    }
};

using python_logging_sink_mt = python_logging_sink<std::mutex>;

}

#endif // AUTOCOG_PYTHON_SINK_HXX
