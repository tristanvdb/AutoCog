#include "autocog/codec/json.hxx"

namespace autocog::codec {
using namespace autocog::data;

// File-local node conversion (FTTNode is FTT's content, used only here).
template <>
nlohmann::json to_json(FTTNode const & n) {
  nlohmann::json j;
  j["action"] = n.action;
  if (n.uid)     j["uid"]     = *n.uid;
  if (n.field)   j["field"]   = *n.field;
  if (n.indices) j["indices"] = *n.indices;
  j["text"]     = n.text;
  j["logprob"]  = n.logprob;
  j["logprobs"] = n.logprobs;
  j["length"]   = n.length;
  j["pruned"]   = n.pruned;
  j["tokens"]   = n.tokens;
  nlohmann::json kids = nlohmann::json::array();
  for (auto const & c : n.children) kids.push_back(to_json(c));
  j["children"] = kids;
  return j;
}
template <>
void from_json(nlohmann::json const & dom, FTTNode & n) {
  autocog::codec::read_guarded("FTTNode", [&]{
  n.action = dom.at("action").get<ActionID>();
  if (dom.contains("uid"))     n.uid     = dom.at("uid").get<std::string>();
  if (dom.contains("field"))   n.field   = dom.at("field").get<int>();
  if (dom.contains("indices")) n.indices = dom.at("indices").get<std::vector<int>>();
  n.text     = dom.at("text").get<std::string>();
  n.logprob  = dom.at("logprob").get<float>();
  n.logprobs = dom.at("logprobs").get<std::vector<float>>();
  n.length   = dom.at("length").get<unsigned>();
  n.pruned   = dom.at("pruned").get<bool>();
  n.tokens   = dom.at("tokens").get<std::vector<TokenID>>();
  for (auto const & c : dom.at("children")) {
    n.children.emplace_back();
    from_json(c, n.children.back());
  }
  });
}

template <>
nlohmann::json to_json(FTT const & s) {
  nlohmann::json j = to_json(s.root);   // root node is the content; siblings below
  if (s.metadata) j["metadata"] = to_json(*s.metadata);
  j["provenance"] = s.provenance;
  return j;
}
template <>
void from_json(nlohmann::json const & dom, FTT & s) {
  autocog::codec::read_guarded("FTT", [&]{
  from_json(dom, s.root);   // FTTNode ignores the metadata/provenance siblings
  if (dom.contains("metadata")) { s.metadata.emplace(); from_json(dom.at("metadata"), *s.metadata); }
  if (dom.contains("provenance"))
    s.provenance = dom.at("provenance").get<std::map<std::string, std::string>>();
  });
}

}
