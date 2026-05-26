#ifndef AUTOCOG_COMPILER_STL_SERIALIZE_HELPERS_HXX
#define AUTOCOG_COMPILER_STL_SERIALIZE_HELPERS_HXX

#include "autocog/compiler/stl/ir.hxx"
#include <nlohmann/json.hpp>

namespace autocog::compiler::stl::serialize {

using json = nlohmann::json;

inline json value_to_json(ir::Value const & value) {
    return std::visit([](auto const & v) -> json {
        using T = std::decay_t<decltype(v)>;
        if constexpr (std::is_same_v<T, int>) return v;
        else if constexpr (std::is_same_v<T, float>) return v;
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
