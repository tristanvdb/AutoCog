#include "autocog/codec/json.hxx"

namespace autocog::codec {
using namespace autocog::data;

template <>
nlohmann::json to_json(PathStep const & ps) {
  nlohmann::json j;
  j["name"] = ps.name;
  if (ps.selector) {
    if (std::holds_alternative<int>(*ps.selector)) {
      j["index"] = std::get<int>(*ps.selector);
    } else {
      auto const & r = std::get<StepRange>(*ps.selector);
      j["slice"] = nlohmann::json::array({
        r.lower ? nlohmann::json(*r.lower) : nlohmann::json(nullptr),
        r.upper ? nlohmann::json(*r.upper) : nlohmann::json(nullptr),
      });
    }
  }
  return j;
}

template <>
void from_json(nlohmann::json const & dom, PathStep & ps) {
  autocog::codec::read_guarded("PathStep", [&]{
  ps.name = dom.at("name").get<std::string>();
  ps.selector.reset();
  if (dom.contains("index") && !dom["index"].is_null()) {
    ps.selector = dom["index"].get<int>();
  } else if (dom.contains("slice") && !dom["slice"].is_null()) {
    StepRange r;
    auto const & sl = dom["slice"];
    if (!sl.at(0).is_null()) r.lower = sl.at(0).get<int>();
    if (!sl.at(1).is_null()) r.upper = sl.at(1).get<int>();
    ps.selector = r;
  }
  });
}

}
