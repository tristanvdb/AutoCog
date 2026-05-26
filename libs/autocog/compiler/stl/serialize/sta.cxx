
#include "autocog/compiler/stl/serialize.hxx"
#include "autocog/compiler/stl/driver.hxx"
#include "autocog/runtime/sta/load.hxx"

namespace autocog::compiler::stl {

void serialize_sta(Driver const & driver, std::ostream & out) {
    auto output = runtime::sta::serialize_program(driver.sta);
    out << output.dump(2) << std::endl;
}

}
