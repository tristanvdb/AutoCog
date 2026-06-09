#include "autocog/codec/json.hxx"

#include "autocog/utilities/errors.hxx"

namespace autocog::codec {
using namespace autocog::data;

namespace {
template <class... Ts> struct overloaded : Ts... { using Ts::operator()...; };
template <class... Ts> overloaded(Ts...) -> overloaded<Ts...>;
}  // namespace

// File-local Action conversion (Action is FTA's node, used only here).
template <>
nlohmann::json to_json(Action const & act) {
  nlohmann::json j;
  j["uid"] = act.uid;
  std::visit(overloaded{
    [&](TextAction const & a) {
      j["type"] = "text";
      j["text"] = a.text;
      if (a.evaluate) j["evaluate"] = a.evaluate;
    },
    [&](CompleteAction const & a) {
      j["type"]      = "complete";
      j["length"]    = a.length;
      j["threshold"] = a.threshold;
      j["beams"]     = a.beams;
      j["ahead"]     = a.ahead;
      j["width"]     = a.width;
      j["stop_text"] = a.stop_text;
      if (a.repetition) j["repetition"] = *a.repetition;
      if (a.diversity)  j["diversity"]  = *a.diversity;
      if (a.vocab)      j["vocab"]      = *a.vocab;
    },
    [&](ChooseAction const & a) {
      j["type"]      = "choose";
      j["choices"]   = a.choices;
      j["threshold"] = a.threshold;
      j["width"]     = a.width;
    },
  }, act.body);
  if (act.field)   j["field"]   = *act.field;
  if (act.indices) j["indices"] = *act.indices;
  j["successors"] = act.successors;
  return j;
}

template <>
void from_json(nlohmann::json const & dom, Action & a) {
  autocog::codec::read_guarded("Action", [&]{
  a.uid = dom.at("uid").get<std::string>();
  std::string type = dom.at("type").get<std::string>();
  if (type == "text") {
    auto & t = a.body.emplace<TextAction>();
    t.text     = dom.value("text", std::string{});
    t.evaluate = dom.value("evaluate", false);
  } else if (type == "complete") {
    auto & c = a.body.emplace<CompleteAction>();
    c.length    = dom.at("length").get<unsigned>();
    c.threshold = dom.at("threshold").get<float>();
    c.beams     = dom.at("beams").get<unsigned>();
    c.ahead     = dom.at("ahead").get<unsigned>();
    c.width     = dom.at("width").get<unsigned>();
    c.stop_text = dom.at("stop_text").get<std::string>();
    if (dom.contains("repetition") && !dom["repetition"].is_null()) c.repetition = dom["repetition"].get<float>();
    if (dom.contains("diversity")  && !dom["diversity"].is_null())  c.diversity  = dom["diversity"].get<float>();
    if (dom.contains("vocab")      && !dom["vocab"].is_null())      c.vocab      = dom["vocab"].get<std::string>();
  } else if (type == "choose") {
    auto & ch = a.body.emplace<ChooseAction>();
    if (dom.contains("choices")) ch.choices = dom.at("choices").get<std::vector<std::string>>();
    ch.threshold = dom.at("threshold").get<float>();
    ch.width     = dom.at("width").get<unsigned>();
  } else {
    throw autocog::SchemaError("autocog::data: unknown action type '" + type + "'", type);
  }
  if (dom.contains("field"))      a.field      = dom.at("field").get<int>();
  if (dom.contains("indices"))    a.indices    = dom.at("indices").get<std::vector<int>>();
  if (dom.contains("successors")) a.successors = dom.at("successors").get<std::vector<std::string>>();
  });
}

template <>
nlohmann::json to_json(FTA const & s) {
  nlohmann::json j;
  nlohmann::json arr = nlohmann::json::array();
  for (auto const & a : s.actions) arr.push_back(to_json(a));
  j["actions"] = arr;
  j["queue"] = nlohmann::json{{"metric", s.queue_metric}};
  if (!s.vocabs.empty()) {
    nlohmann::json vj = nlohmann::json::object();
    for (auto const & [k, ve] : s.vocabs) vj[k] = to_json(ve);
    j["vocabs"] = vj;
  }
  if (s.metadata) j["metadata"] = to_json(*s.metadata);
  j["provenance"] = s.provenance;
  return j;
}

template <>
void from_json(nlohmann::json const & dom, FTA & s) {
  autocog::codec::read_guarded("FTA", [&]{
  for (auto const & aj : dom.at("actions")) {
    s.actions.emplace_back();
    from_json(aj, s.actions.back());
  }
  if (dom.contains("queue") && dom["queue"].contains("metric"))
    s.queue_metric = dom["queue"]["metric"].get<std::string>();
  if (dom.contains("vocabs"))
    for (auto const & [k, vj] : dom["vocabs"].items()) from_json(vj, s.vocabs[k]);
  if (dom.contains("metadata")) { s.metadata.emplace(); from_json(dom.at("metadata"), *s.metadata); }
  if (dom.contains("provenance"))
    s.provenance = dom.at("provenance").get<std::map<std::string, std::string>>();
  });
}

}
