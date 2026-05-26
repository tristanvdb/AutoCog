
#include "autocog/compiler/stl/driver.hxx"
#include "autocog/compiler/stl/serialize.hxx"

#include <fstream>

using namespace autocog::compiler::stl;

std::optional<int> parse_args(int argc, char** argv, Driver & driver);

int main(int argc, char** argv) {
    Driver driver;
    auto retval = parse_args(argc, argv, driver);
    if (retval) return retval.value();

    retval = driver.compile();
    if (retval) return retval.value();

    // Serialize
    std::ostream * out = &std::cout;
    std::ofstream outfile;
    if (driver.output) {
        outfile.open(*driver.output);
        if (!outfile) {
            std::cerr << "Error: Cannot write to output file: " << *driver.output << std::endl;
            return 1;
        }
        out = &outfile;
    }

    switch (driver.stage) {
        case CompilationStage::Parse:       serialize_ast(driver, *out);     break;
        case CompilationStage::Symbols:     serialize_symbols(driver, *out); break;
        case CompilationStage::Globals:     serialize_globals(driver, *out); break;
        case CompilationStage::Instantiate: serialize_graph(driver, *out);   break;
        case CompilationStage::Assemble:    serialize_ir(driver, *out);      break;
        case CompilationStage::Generate:    serialize_sta(driver, *out);     break;
    }

    return 0;
}
