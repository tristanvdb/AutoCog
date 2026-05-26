
#include "autocog/compiler/stl/serialize.hxx"
#include "autocog/compiler/stl/driver.hxx"
#include "helpers.hxx"

namespace autocog::compiler::stl {

void serialize_graph(Driver const & driver, std::ostream & out) {
    using json = serialize::json;

    json output;

    output["entry_points"] = json::object();
    for (auto const & [entry_name, mangled] : driver.entry_point_map) {
        output["entry_points"][entry_name] = mangled;
    }

    output["nodes"] = json::object();
    for (auto const & [name, node] : driver.graph.nodes) {
        json n;
        n["mangled_name"] = node.mangled_name;
        n["base_name"] = node.base_name;
        n["fileid"] = node.fileid;
        n["arguments"] = serialize::varmap_to_json(node.arguments);
        n["context"] = serialize::varmap_to_json(node.context);
        std::visit([&](auto const & d) {
            using T = std::decay_t<decltype(d.get())>;
            if constexpr (std::is_same_v<T, ast::Record>) {
                n["kind"] = "record";
            } else {
                n["kind"] = "prompt";
            }
        }, node.ast);
        output["nodes"][name] = n;
    }

    output["edges"] = json::array();
    for (auto const & edge : driver.graph.edges) {
        json e;
        e["from"] = edge.from;
        e["to"] = edge.to;
        output["edges"].push_back(e);
    }

    out << output.dump(2) << std::endl;
}

}
