#ifndef AUTOCOG_PYTHON_SINK_HXX
#define AUTOCOG_PYTHON_SINK_HXX

#include <spdlog/sinks/base_sink.h>
#include <pybind11/pybind11.h>
#include <mutex>
#include <string>

namespace py = pybind11;

namespace autocog {

// Custom spdlog sink that forwards log records to Python's logging module.
// Looks up the Python logger on each call to avoid destructor ordering issues.
template <typename Mutex>
class python_logging_sink : public spdlog::sinks::base_sink<Mutex> {
protected:
    void sink_it_(const spdlog::details::log_msg & msg) override {
        try {
            py::gil_scoped_acquire gil;
            auto py_logger = py::module_::import("logging").attr("getLogger")("autocog");
            int py_level = to_python_level(msg.level);
            std::string text(msg.payload.data(), msg.payload.size());
            py_logger.attr("log")(py_level, text);
        } catch (...) {
            // Interpreter may be finalizing — silently drop the record
        }
    }

    void flush_() override {}

private:
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
