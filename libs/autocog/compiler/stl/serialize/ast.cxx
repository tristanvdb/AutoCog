
#include "autocog/compiler/stl/serialize.hxx"
#include "autocog/compiler/stl/driver.hxx"
#include "helpers.hxx"

namespace autocog::compiler::stl {

void serialize_ast(Driver const & driver, std::ostream & out) {
    using json = serialize::json;

    json output;
    output["files"] = json::array();
    for (auto const & program : driver.programs) {
        json file;
        file["fid"] = program.data.fid;
        file["filename"] = program.data.filename;
        output["files"].push_back(file);
    }

    out << output.dump(2) << std::endl;
}

}
