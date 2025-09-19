
#include "convert.hxx"

#include "autocog/compiler/stl/driver.hxx"

using namespace autocog::compiler::stl;

std::optional<int> parse_args(int argc, char** argv, Driver & driver);

int main(int argc, char** argv) {
    Driver driver;
    auto retval = parse_args(argc, argv, driver);
    if (retval) return retval.value();
    retval = driver.compile();
    if (retval) return retval.value();
    return driver.backend();
}
