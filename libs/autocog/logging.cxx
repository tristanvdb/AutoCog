
#include "autocog/logging.hxx"

#include <spdlog/sinks/stdout_sinks.h>
#include <spdlog/sinks/null_sink.h>

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

}
