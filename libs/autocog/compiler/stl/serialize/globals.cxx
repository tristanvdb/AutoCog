
#include "autocog/compiler/stl/serialize.hxx"
#include "autocog/compiler/stl/driver.hxx"
#include "helpers.hxx"

namespace autocog::compiler::stl {

void serialize_globals(Driver const & driver, std::ostream & out) {
    using json = serialize::json;

    json output;
    output["contexts"] = json::object();
    for (auto const & [scope, context] : driver.tables.contexts) {
        output["contexts"][scope] = serialize::varmap_to_json(context);
    }

    out << output.dump(2) << std::endl;
}

}
