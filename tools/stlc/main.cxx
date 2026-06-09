
#include "autocog/compiler/stl/driver.hxx"
#include "autocog/compiler/stl/serialize.hxx"
#include "autocog/logging.hxx"
#include "autocog/utilities/exception.hxx"

#include <fstream>
#include <iostream>
#include <optional>
#include <utility>

using namespace autocog::compiler::stl;

std::optional<int> parse_args(int argc, char** argv, Driver & driver);

static int run(int argc, char** argv) {
    autocog::init_console_logger();  // default level; --verbose overrides
    Driver driver;
    auto retval = parse_args(argc, argv, driver);
    if (retval) return retval.value();

    retval = driver.compile();
    if (retval) return retval.value();

    // Serialize each requested emit to its file. Several may be requested in one
    // run; compile() already ran up to the deepest requested stage.
    using Emit = std::pair<std::optional<std::string> const &,
                           void (*)(Driver const &, std::ostream &)>;
    Emit const emits[] = {
        { driver.out_ast,   serialize_ast   },
        { driver.out_graph, serialize_graph },
        { driver.out_ir,    serialize_ir    },
        { driver.out_sta,   serialize_sta   },
    };
    for (auto const & [path, serialize] : emits) {
        if (!path) continue;
        std::ofstream outfile;
        std::ostream * out = &std::cout;
        if (*path != "/dev/stdout") {
            outfile.open(*path);
            if (!outfile) {
                std::cerr << "Error: Cannot write to output file: " << *path << std::endl;
                return 1;
            }
            out = &outfile;
        }
        serialize(driver, *out);
    }

    return 0;
}

int main(int argc, char** argv) {
    return autocog::utilities::guard_main([&]{ return run(argc, argv); });
}
