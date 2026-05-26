
#include "autocog/compiler/stl/driver.hxx"
#include "autocog/compiler/stl/evaluate.hxx"
#include "autocog/compiler/stl/instantiation-graph.hxx"

namespace autocog::compiler::stl {

std::optional<int> Driver::run_instantiate() {
    InstantiationGraphBuilder builder(*this, *evaluator, graph);
    builder.build();
    if (report_errors()) return 4;

#if !defined(NDEBUG)
    std::cerr << "After building instantiation graph (#4):" << std::endl;
    std::cerr << "  " << graph.nodes.size() << " nodes, "
              << graph.edges.size() << " edges" << std::endl;
    for (auto const & [name, node] : graph.nodes) {
        std::cerr << "    " << name;
        std::visit([](auto const & d) {
            using T = std::decay_t<decltype(d.get())>;
            if constexpr (std::is_same_v<T, ast::Record>) {
                std::cerr << " [Record]";
            } else {
                std::cerr << " [Prompt]";
            }
        }, node.ast);
        std::cerr << std::endl;
        for (auto const & [k, v] : node.arguments) {
            std::cerr << "      " << k << " = ";
            std::visit([](auto const & val) {
                using V = std::decay_t<decltype(val)>;
                if constexpr (std::is_same_v<V, int>) std::cerr << val;
                else if constexpr (std::is_same_v<V, float>) std::cerr << val;
                else if constexpr (std::is_same_v<V, bool>) std::cerr << (val ? "true" : "false");
                else if constexpr (std::is_same_v<V, std::string>) std::cerr << "\"" << val << "\"";
                else std::cerr << "null";
            }, v);
            std::cerr << std::endl;
        }
    }
    for (auto const & edge : graph.edges) {
        std::cerr << "    " << edge.from << " -> " << edge.to << std::endl;
    }
#endif

    return std::nullopt;
}

} // namespace autocog::compiler::stl
