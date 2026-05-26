
#include "autocog/compiler/stl/driver.hxx"
#include "autocog/compiler/stl/parser.hxx"
#include "autocog/compiler/stl/ast/printer.hxx"

namespace autocog::compiler::stl {

std::optional<int> Driver::run_parse() {
    Parser parser(diagnostics, fileids, includes, programs, inputs);
    parser.parse();
    if (report_errors()) return 1;

#if !defined(NDEBUG)
    std::cerr << "After parsing (#1):" << std::endl;
    for (auto const & program : programs) {
        std::cerr << "  " << program.data.fid << ": " << program.data.filename << std::endl;
        ast::printTagTree(program, std::cerr, "> ");
    }
#endif

    return std::nullopt;
}

} // namespace autocog::compiler::stl
