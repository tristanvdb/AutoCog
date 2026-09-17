
#include "autocog/compiler/stl/driver.hxx"
#include "autocog/compiler/stl/parser.hxx"
#include "autocog/logging.hxx"

#include <algorithm>

namespace autocog::compiler::stl {

std::optional<int> Driver::run_parse() {
    // Each input's own directory is an implicit include, ahead of the rest:
    // `from "record.stl" import choice;` means the file sitting next to the one
    // importing it. This lives here rather than in a caller so that stlc, the
    // Python binding, and anything else embedding the driver agree on what a
    // quoted name refers to — the Python side had grown its own copy of the
    // rule for the .py imports, and the two forms must not disagree.
    for (auto const & input : inputs) {
        auto slash = input.find_last_of('/');
        std::string dir = (slash == std::string::npos) ? "." : input.substr(0, slash);
        if (std::find(includes.begin(), includes.end(), dir) == includes.end())
            includes.push_front(dir);
    }

    Parser parser(diagnostics, fileids, includes, programs, inputs);
    parser.parse();
    if (report_errors()) return 1;

    SPDLOG_LOGGER_DEBUG(autocog::log(), "Parsed {} program(s) (#1)", programs.size());

    return std::nullopt;
}

} // namespace autocog::compiler::stl
