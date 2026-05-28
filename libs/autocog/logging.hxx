#ifndef AUTOCOG_LOGGING_HXX
#define AUTOCOG_LOGGING_HXX

#include <spdlog/spdlog.h>

namespace autocog {

// Default log level: INFO in Debug, WARNING in Release
#ifdef NDEBUG
constexpr auto default_level = spdlog::level::warn;
#else
constexpr auto default_level = spdlog::level::info;
#endif

// Shared "autocog" logger. Created lazily with a null sink;
// the entry point (tool main or pybind11 module) installs the real sink.
std::shared_ptr<spdlog::logger> log();

// Install a logger with the given sink and level.
void init_logger(spdlog::sink_ptr sink, spdlog::level::level_enum lvl = default_level);

// Install a stderr color sink — called by C++ tools (stlc, ista, xfta, psta).
void init_console_logger(spdlog::level::level_enum lvl = default_level);

// Set the logger level.
void set_level(spdlog::level::level_enum lvl);

}

#endif // AUTOCOG_LOGGING_HXX
