#ifndef AUTOCOG_LOGGING_HXX
#define AUTOCOG_LOGGING_HXX

#include <spdlog/spdlog.h>

#include <string>

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

// True if `tok` looks like a verbosity-level word: a bare alphabetic token
// (trace/debug/info/warn/error/critical/off), not a flag ('-...') and not a
// path-like positional (containing '.', '/', digits, etc.). Used by the CLI
// tools to tell "-V debug" (consume the level) from "-V foo.stl" (bare -V,
// the path is the next positional).
bool looks_like_level_token(std::string const & tok);

// Parse a verbosity-level word into a level. Returns false if `tok` is not a
// recognized level (the caller should report an "unknown verbosity level"
// error). "off" is recognized; an unrecognized word is rejected rather than
// silently mapped to off.
bool parse_level(std::string const & tok, spdlog::level::level_enum & out);

}

#endif // AUTOCOG_LOGGING_HXX
