#include "autocog/codec/json.hxx"

namespace autocog::codec {
using namespace autocog::data;

// File-local sub-struct conversions.
template <>
nlohmann::json to_json(TextSearch const & t) {
  nlohmann::json j;
  j["threshold"] = t.threshold;
  j["beams"]     = t.beams;
  j["ahead"]     = t.ahead;
  j["width"]     = t.width;
  if (t.repetition) j["repetition"] = *t.repetition;
  if (t.diversity)  j["diversity"]  = *t.diversity;
  return j;
}
template <>
void from_json(nlohmann::json const & t, TextSearch & out) {
  autocog::codec::read_guarded("TextSearch", [&]{
  out.threshold = t.at("threshold").get<float>();
  out.beams     = t.at("beams").get<unsigned>();
  out.ahead     = t.at("ahead").get<unsigned>();
  out.width     = t.at("width").get<unsigned>();
  if (t.contains("repetition") && !t.at("repetition").is_null()) out.repetition = t.at("repetition").get<float>();
  if (t.contains("diversity")  && !t.at("diversity").is_null())  out.diversity  = t.at("diversity").get<float>();
  });
}

template <>
nlohmann::json to_json(ChoiceSearch const & c) {
  return nlohmann::json{{"threshold", c.threshold}, {"width", c.width}};
}
template <>
void from_json(nlohmann::json const & c, ChoiceSearch & out) {
  autocog::codec::read_guarded("ChoiceSearch", [&]{
  out.threshold = c.at("threshold").get<float>();
  out.width     = c.at("width").get<unsigned>();
  });
}

template <>
nlohmann::json to_json(QueueSearch const & q) {
  return nlohmann::json{{"metric", q.metric}};
}
template <>
void from_json(nlohmann::json const & q, QueueSearch & out) {
  autocog::codec::read_guarded("QueueSearch", [&]{
  out.metric = q.at("metric").get<std::string>();
  });
}

template <>
nlohmann::json to_json(SearchConfig const & s) {
  nlohmann::json j;
  j["text"]   = to_json(s.text);
  j["enum"]   = to_json(s.enums);
  j["branch"] = to_json(s.branch);
  j["flow"]   = to_json(s.flow);
  j["queue"]  = to_json(s.queue);
  if (s.metadata) j["metadata"] = to_json(*s.metadata);
  j["provenance"] = s.provenance;
  return j;
}
template <>
void from_json(nlohmann::json const & dom, SearchConfig & s) {
  autocog::codec::read_guarded("SearchConfig", [&]{
  from_json(dom.at("text"),   s.text);
  from_json(dom.at("enum"),   s.enums);
  from_json(dom.at("branch"), s.branch);
  from_json(dom.at("flow"),   s.flow);
  from_json(dom.at("queue"),  s.queue);
  if (dom.contains("metadata")) { s.metadata.emplace(); from_json(dom.at("metadata"), *s.metadata); }
  if (dom.contains("provenance"))
    s.provenance = dom.at("provenance").get<std::map<std::string, std::string>>();
  });
}

}
