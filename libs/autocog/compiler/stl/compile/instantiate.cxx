
#include "autocog/compiler/stl/driver.hxx"
#include "autocog/compiler/stl/evaluate.hxx"
#include "autocog/compiler/stl/instantiation-graph.hxx"
#include "autocog/logging.hxx"

namespace autocog::compiler::stl {

std::optional<int> Driver::run_instantiate() {
    InstantiationGraphBuilder builder(*this, *evaluator, graph);
    builder.build();
    if (report_errors()) return 4;

    SPDLOG_LOGGER_DEBUG(autocog::log(), "Instantiation graph (#4): {} nodes, {} edges",
                        graph.nodes.size(), graph.edges.size());

    return std::nullopt;
}

} // namespace autocog::compiler::stl
