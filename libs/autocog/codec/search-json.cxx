#include "autocog/codec/json.hxx"

#include "autocog/data/search-registry.hxx"

namespace autocog::codec {
using namespace autocog::data;

namespace {
// Domain checks against the registry (the choice categories share domains,
// so the "enum" rows stand for all three).
void check_score_metric(char const * key, std::string const & value) {
  auto const * param = registry::find("enum", key);
  if (param && !registry::in_domain(*param, value))
    throw autocog::SchemaError(
        std::string("search: invalid ") + key + " '" + value + "' (allowed: "
        + registry::domain_text(*param) + ")", value);
}
void check_queue_metric(std::string const & value) {
  auto const * param = registry::find("queue", "metric");
  if (param && !registry::in_domain(*param, value))
    throw autocog::SchemaError(
        "search: unknown queue metric '" + value + "' (allowed: "
        + registry::domain_text(*param) + ")", value);
}
}

// File-local sub-struct conversions.
template <>
nlohmann::json to_json(TextSearch const & t) {
  nlohmann::json j;
  j["threshold"] = t.threshold;
  j["beams"]     = t.beams;
  if (t.topk) j["topk"] = *t.topk;
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
  if (t.contains("topk") && !t.at("topk").is_null()) out.topk = t.at("topk").get<unsigned>();
  out.ahead     = t.at("ahead").get<unsigned>();
  out.width     = t.at("width").get<unsigned>();
  if (t.contains("repetition") && !t.at("repetition").is_null()) out.repetition = t.at("repetition").get<float>();
  if (t.contains("diversity")  && !t.at("diversity").is_null())  out.diversity  = t.at("diversity").get<float>();
  });
}

template <>
nlohmann::json to_json(ChoiceSearch const & c) {
  nlohmann::json j;
  // Scalar threshold is the shorthand for the default metric; the object
  // form carries a non-default one.
  if (c.threshold_metric != "mean")
    j["threshold"] = nlohmann::json{{"value", c.threshold},
                                    {"metric", c.threshold_metric}};
  else
    j["threshold"] = c.threshold;
  j["width"] = c.width;
  if (c.ranking != "mean") j["ranking"] = nlohmann::json{{"metric", c.ranking}};
  return j;
}
template <>
void from_json(nlohmann::json const & c, ChoiceSearch & out) {
  autocog::codec::read_guarded("ChoiceSearch", [&]{
  auto const & th = c.at("threshold");
  if (th.is_object()) {
    out.threshold = th.at("value").get<float>();
    if (th.contains("metric") && !th.at("metric").is_null())
      out.threshold_metric = th.at("metric").get<std::string>();
  } else {
    out.threshold = th.get<float>();
  }
  out.width = c.at("width").get<unsigned>();
  if (c.contains("ranking") && !c.at("ranking").is_null()) {
    auto const & r = c.at("ranking");
    out.ranking = r.is_object() ? r.at("metric").get<std::string>()
                                : r.get<std::string>();
  }
  check_score_metric("threshold.metric", out.threshold_metric);
  check_score_metric("ranking.metric", out.ranking);
  });
}

template <>
nlohmann::json to_json(QueueSearch const & q) {
  nlohmann::json j{{"metric", q.metric}};
  if (q.stop) j["stop"] = to_json(*q.stop);
  return j;
}
template <>
void from_json(nlohmann::json const & q, QueueSearch & out) {
  autocog::codec::read_guarded("QueueSearch", [&]{
  // A single metric may be given as a bare string; a list is lexicographic.
  auto const & m = q.at("metric");
  out.metric.clear();
  if (m.is_string()) out.metric.push_back(m.get<std::string>());
  else out.metric = m.get<std::vector<std::string>>();
  for (auto const & name : out.metric) check_queue_metric(name);
  if (q.contains("stop") && !q.at("stop").is_null()) {
    out.stop.emplace();
    from_json(q.at("stop"), *out.stop);
  }
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
