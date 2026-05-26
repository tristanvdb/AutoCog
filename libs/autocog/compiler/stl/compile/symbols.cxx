
#include "autocog/compiler/stl/driver.hxx"
#include "autocog/compiler/stl/symbol-scanner.hxx"

namespace autocog::compiler::stl {

std::optional<int> Driver::run_symbols() {
    SymbolScanner symbol_scanner(*this);
    traverse_ast(symbol_scanner);
    if (report_errors()) return 2;

#if !defined(NDEBUG)
    std::cerr << "After collecting symbol (#2):" << std::endl;
    tables.dump_symbols(std::cerr);
#endif

    return std::nullopt;
}

} // namespace autocog::compiler::stl
