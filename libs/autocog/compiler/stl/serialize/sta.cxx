
#include "autocog/compiler/stl/serialize.hxx"
#include "autocog/compiler/stl/driver.hxx"
#include "autocog/runtime/sta/load.hxx"
#include "autocog/utilities/metadata.hxx"

#include <fstream>
#include <sstream>

namespace autocog::compiler::stl {

void serialize_sta(Driver const & driver, std::ostream & out) {
    // Compute source_uid from the main input file
    std::string source_uid;
    if (!driver.inputs.empty()) {
        std::ifstream ifs(driver.inputs.front());
        std::ostringstream ss;
        ss << ifs.rdbuf();
        source_uid = autocog::compute_source_uid(ss.str());
    }
    auto output = runtime::sta::serialize_program(driver.sta, source_uid);
    out << output.dump(2) << std::endl;
}

}
