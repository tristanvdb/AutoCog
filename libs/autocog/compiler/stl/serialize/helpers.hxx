#ifndef AUTOCOG_COMPILER_STL_SERIALIZE_HELPERS_HXX
#define AUTOCOG_COMPILER_STL_SERIALIZE_HELPERS_HXX

#include "autocog/compiler/stl/ir.hxx"
#include <nlohmann/json.hpp>
#include <cmath>

namespace autocog::compiler::stl::serialize {

using json = nlohmann::json;

// Canonical float form for generated JSON: round to 3 decimal places. This
// gives a single deterministic representation that C++ and Python both produce
// identically (avoiding float32/float64 display divergence). Integers are
// unaffected; only floating-point values are rounded.
inline double round3(float v) {
    return std::round(static_cast<double>(v) * 1000.0) / 1000.0;
}

inline json value_to_json(ir::Value const & value) {
    return std::visit([](auto const & v) -> json {
        using T = std::decay_t<decltype(v)>;
        if constexpr (std::is_same_v<T, int>) return v;
        else if constexpr (std::is_same_v<T, float>) return round3(v);
        else if constexpr (std::is_same_v<T, bool>) return v;
        else if constexpr (std::is_same_v<T, std::string>) return v;
        else if constexpr (std::is_same_v<T, std::nullptr_t>) return nullptr;
    }, value);
}

inline json varmap_to_json(ir::VarMap const & varmap) {
    json j = json::object();
    for (auto const & [name, value] : varmap) {
        j[name] = value_to_json(value);
    }
    return j;
}

}

#endif // AUTOCOG_COMPILER_STL_SERIALIZE_HELPERS_HXX
