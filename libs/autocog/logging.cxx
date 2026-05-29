
#include "autocog/logging.hxx"

#include <spdlog/sinks/stdout_sinks.h>
#include <spdlog/sinks/null_sink.h>

#include <algorithm>
#include <cctype>

namespace autocog {

static std::shared_ptr<spdlog::logger> g_logger;

std::shared_ptr<spdlog::logger> log() {
    if (!g_logger) {
        auto sink = std::make_shared<spdlog::sinks::null_sink_mt>();
        g_logger = std::make_shared<spdlog::logger>("autocog", sink);
        g_logger->set_level(default_level);
    }
    return g_logger;
}

void init_logger(spdlog::sink_ptr sink, spdlog::level::level_enum lvl) {
    g_logger = std::make_shared<spdlog::logger>("autocog", sink);
    g_logger->set_level(lvl);
    g_logger->set_pattern("[%L] %v");
}

void init_console_logger(spdlog::level::level_enum lvl) {
    auto sink = std::make_shared<spdlog::sinks::stderr_sink_mt>();
    init_logger(sink, lvl);
}

void set_level(spdlog::level::level_enum lvl) {
    log()->set_level(lvl);
}

bool looks_like_level_token(std::string const & tok) {
    if (tok.empty() || tok[0] == '-') return false;
    for (char c : tok) {
        if (!std::isalpha(static_cast<unsigned char>(c))) return false;
    }
    return true;
}

bool parse_level(std::string const & tok, spdlog::level::level_enum & out) {
    std::string lower = tok;
    std::transform(lower.begin(), lower.end(), lower.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    auto parsed = spdlog::level::from_str(lower);
    // from_str maps anything unrecognized to `off`, so only accept that result
    // when the token literally said "off".
    if (parsed == spdlog::level::off && lower != "off") return false;
    out = parsed;
    return true;
}

}
