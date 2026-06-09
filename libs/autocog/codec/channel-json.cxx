#include "autocog/codec/json.hxx"

#include "autocog/utilities/errors.hxx"

namespace autocog::codec {
using namespace autocog::data;

// File-local sub-struct conversions (definitions below); Channel is in json.hxx.
template <> nlohmann::json to_json(Clause const &);
template <> void from_json(nlohmann::json const &, Clause &);
template <> nlohmann::json to_json(ChannelKwarg const &);
template <> void from_json(nlohmann::json const &, ChannelKwarg &);

namespace {

template <class... Ts> struct overloaded : Ts... { using Ts::operator()...; };
template <class... Ts> overloaded(Ts...) -> overloaded<Ts...>;

nlohmann::json paths_to_json(std::vector<PathStep> const & v) {
  nlohmann::json a = nlohmann::json::array();
  for (auto const & p : v) a.push_back(to_json(p));
  return a;
}
void paths_from_json(nlohmann::json const & a, std::vector<PathStep> & out) {
  for (auto const & e : a) { out.emplace_back(); from_json(e, out.back()); }
}
nlohmann::json clauses_to_json(std::vector<Clause> const & v) {
  nlohmann::json a = nlohmann::json::array();
  for (auto const & c : v) a.push_back(to_json(c));
  return a;
}
void clauses_from_json(nlohmann::json const & a, std::vector<Clause> & out) {
  for (auto const & e : a) { out.emplace_back(); from_json(e, out.back()); }
}

}  // namespace

template <>
nlohmann::json to_json(Clause const & c) {
  return std::visit(overloaded{
    [](BindClause const & x) -> nlohmann::json {
      return {{"type","bind"}, {"source", paths_to_json(x.source)}, {"target", paths_to_json(x.target)}};
    },
    [](RavelClause const & x) -> nlohmann::json {
      nlohmann::json j = {{"type","ravel"}, {"target", paths_to_json(x.target)}};
      if (x.depth) j["depth"] = *x.depth;
      return j;
    },
    [](WrapClause const & x) -> nlohmann::json {
      return {{"type","wrap"}, {"target", paths_to_json(x.target)}};
    },
    [](PruneClause const & x) -> nlohmann::json {
      return {{"type","prune"}, {"target", paths_to_json(x.target)}};
    },
    [](MappedClause const & x) -> nlohmann::json {
      return {{"type","mapped"}, {"target", paths_to_json(x.target)}};
    },
  }, c.value);
}

template <>
void from_json(nlohmann::json const & dom, Clause & out) {
  autocog::codec::read_guarded("Clause", [&]{
  std::string type = dom.at("type").get<std::string>();
  if (type == "bind") {
    auto & x = out.value.emplace<BindClause>();
    paths_from_json(dom.at("source"), x.source);
    paths_from_json(dom.at("target"), x.target);
  } else if (type == "ravel") {
    auto & x = out.value.emplace<RavelClause>();
    if (dom.contains("depth")) x.depth = dom.at("depth").get<int>();
    paths_from_json(dom.at("target"), x.target);
  } else if (type == "wrap") {
    auto & x = out.value.emplace<WrapClause>();
    paths_from_json(dom.at("target"), x.target);
  } else if (type == "prune") {
    auto & x = out.value.emplace<PruneClause>();
    paths_from_json(dom.at("target"), x.target);
  } else if (type == "mapped") {
    auto & x = out.value.emplace<MappedClause>();
    paths_from_json(dom.at("target"), x.target);
  } else {
    throw autocog::SchemaError("autocog::data: unknown clause type '" + type + "'", type);
  }
  });
}

template <>
nlohmann::json to_json(ChannelKwarg const & k) {
  nlohmann::json j;
  j["name"]     = k.name;
  j["is_input"] = k.is_input;
  j["path"]     = paths_to_json(k.path);
  j["clauses"]  = clauses_to_json(k.clauses);
  j["prompt"]   = k.prompt ? nlohmann::json(*k.prompt) : nlohmann::json(nullptr);
  j["value"]    = k.value  ? nlohmann::json(*k.value)  : nlohmann::json(nullptr);
  return j;
}

template <>
void from_json(nlohmann::json const & dom, ChannelKwarg & k) {
  autocog::codec::read_guarded("ChannelKwarg", [&]{
  k.name     = dom.at("name").get<std::string>();
  k.is_input = dom.at("is_input").get<bool>();
  paths_from_json(dom.at("path"), k.path);
  clauses_from_json(dom.at("clauses"), k.clauses);
  if (dom.contains("prompt") && !dom["prompt"].is_null()) k.prompt = dom["prompt"].get<std::string>();
  if (dom.contains("value")  && !dom["value"].is_null())  k.value  = dom["value"].get<std::string>();
  });
}

template <>
nlohmann::json to_json(Channel const & c) {
  return std::visit(overloaded{
    [](InputChannel const & x) -> nlohmann::json {
      return {{"type","input"}, {"target", paths_to_json(x.target)}, {"source", paths_to_json(x.source)}};
    },
    [](DataflowChannel const & x) -> nlohmann::json {
      nlohmann::json j = {{"type","dataflow"}, {"target", paths_to_json(x.target)},
                          {"source", paths_to_json(x.source)}, {"clauses", clauses_to_json(x.clauses)}};
      j["prompt"] = x.prompt ? nlohmann::json(*x.prompt) : nlohmann::json(nullptr);
      return j;
    },
    [](CallChannel const & x) -> nlohmann::json {
      nlohmann::json kwargs = nlohmann::json::array();
      for (auto const & kw : x.kwargs) kwargs.push_back(to_json(kw));
      nlohmann::json j = {{"type","call"}, {"target", paths_to_json(x.target)},
                          {"kwargs", kwargs}, {"clauses", clauses_to_json(x.clauses)}};
      j["extern"] = x.extern_func ? nlohmann::json(*x.extern_func) : nlohmann::json(nullptr);
      j["entry"]  = x.entry       ? nlohmann::json(*x.entry)       : nlohmann::json(nullptr);
      return j;
    },
  }, c.value);
}

template <>
void from_json(nlohmann::json const & dom, Channel & out) {
  autocog::codec::read_guarded("Channel", [&]{
  std::string type = dom.at("type").get<std::string>();
  if (type == "input") {
    auto & x = out.value.emplace<InputChannel>();
    paths_from_json(dom.at("target"), x.target);
    paths_from_json(dom.at("source"), x.source);
  } else if (type == "dataflow") {
    auto & x = out.value.emplace<DataflowChannel>();
    paths_from_json(dom.at("target"), x.target);
    paths_from_json(dom.at("source"), x.source);
    clauses_from_json(dom.at("clauses"), x.clauses);
    if (dom.contains("prompt") && !dom["prompt"].is_null()) x.prompt = dom["prompt"].get<std::string>();
  } else if (type == "call") {
    auto & x = out.value.emplace<CallChannel>();
    paths_from_json(dom.at("target"), x.target);
    clauses_from_json(dom.at("clauses"), x.clauses);
    for (auto const & kj : dom.at("kwargs")) { x.kwargs.emplace_back(); from_json(kj, x.kwargs.back()); }
    if (dom.contains("extern") && !dom["extern"].is_null()) x.extern_func = dom["extern"].get<std::string>();
    if (dom.contains("entry")  && !dom["entry"].is_null())  x.entry       = dom["entry"].get<std::string>();
  } else {
    throw autocog::SchemaError("autocog::data: unknown channel type '" + type + "'", type);
  }
  });
}

}
