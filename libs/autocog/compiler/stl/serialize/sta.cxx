
#include "autocog/compiler/stl/serialize.hxx"
#include "autocog/compiler/stl/driver.hxx"
#include "autocog/codec/json.hxx"

#include <ostream>

namespace autocog::compiler::stl {

void serialize_sta(Driver const & driver, std::ostream & out) {
    // STA is a root artifact; its metadata was stamped by run_generate()
    // (finalize). Serialize the data:: program through its JSON conversion.
    out << autocog::codec::to_json(driver.sta).dump(2) << std::endl;
}

}
