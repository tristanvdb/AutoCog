#include "autocog/codec/json.hxx"
#include "autocog/utilities/types.hxx"

#include <type_traits>

namespace autocog::codec {

template <> nlohmann::json to_json(autocog::types::Document const & doc) {
  using D = autocog::types::Document;
  if (auto const * v = std::get_if<autocog::types::Value>(&doc.value)) {
    return std::visit([](auto const & x) -> nlohmann::json {
      using T = std::decay_t<decltype(x)>;
      if constexpr (std::is_same_v<T, std::monostate>) return nullptr;
      else return x;
    }, *v);
  }
  if (auto const * a = std::get_if<D::Array>(&doc.value)) {
    nlohmann::json arr = nlohmann::json::array();
    for (auto const & e : *a) arr.push_back(to_json(e));
    return arr;
  }
  nlohmann::json obj = nlohmann::json::object();
  for (auto const & [k, val] : std::get<D::Object>(doc.value)) obj[k] = to_json(val);
  return obj;
}

template <> void from_json(nlohmann::json const & j, autocog::types::Document & doc) {
  autocog::codec::read_guarded("Document", [&]{
  using D = autocog::types::Document;
  if (j.is_object()) {
    D::Object o;
    for (auto it = j.begin(); it != j.end(); ++it) {
      D child; from_json(it.value(), child);
      o.emplace(it.key(), std::move(child));
    }
    doc.value = std::move(o);
  } else if (j.is_array()) {
    D::Array a; a.reserve(j.size());
    for (auto const & e : j) { D child; from_json(e, child); a.push_back(std::move(child)); }
    doc.value = std::move(a);
  } else {
    autocog::types::Value v;
    if      (j.is_number_integer()) v = j.get<int>();
    else if (j.is_number_float())   v = j.get<float>();
    else if (j.is_boolean())        v = j.get<bool>();
    else if (j.is_string())         v = j.get<std::string>();
    else                            v = std::monostate{};
    doc.value = v;
  }
  });
}

}
