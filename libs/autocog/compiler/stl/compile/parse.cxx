
#include "autocog/compiler/stl/driver.hxx"
#include "autocog/compiler/stl/parser.hxx"
#include "autocog/logging.hxx"

namespace autocog::compiler::stl {

std::optional<int> Driver::run_parse() {
    Parser parser(diagnostics, fileids, includes, programs, inputs);
    parser.parse();
    if (report_errors()) return 1;

    SPDLOG_LOGGER_DEBUG(autocog::log(), "Parsed {} program(s) (#1)", programs.size());

    return std::nullopt;
}

} // namespace autocog::compiler::stl
