
#include "autocog/compiler/stl/driver.hxx"
#include "autocog/compiler/stl/symbol-scanner.hxx"
#include "autocog/logging.hxx"

namespace autocog::compiler::stl {

std::optional<int> Driver::run_symbols() {
    SymbolScanner symbol_scanner(*this);
    traverse_ast(symbol_scanner);
    if (report_errors()) return 2;

    SPDLOG_LOGGER_DEBUG(autocog::log(), "Symbols collected (#2)");

    return std::nullopt;
}

} // namespace autocog::compiler::stl
