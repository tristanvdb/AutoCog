#include "autocog/codec/json.hxx"

#include "autocog/utilities/errors.hxx"

#include <cstdint>

namespace autocog::codec {
using namespace autocog::data;

// File-local sub-struct conversions (definitions below). STA itself is in json.hxx.
template <> nlohmann::json to_json(SearchParams const &);
template <> void from_json(nlohmann::json const &, SearchParams &);
template <> nlohmann::json to_json(FieldFormat const &);
template <> void from_json(nlohmann::json const &, FieldFormat &);
template <> nlohmann::json to_json(Flow const &);
template <> void from_json(nlohmann::json const &, Flow &);
template <> nlohmann::json to_json(Field const &);
template <> void from_json(nlohmann::json const &, Field &);
template <> nlohmann::json to_json(SchemaField const &);
template <> void from_json(nlohmann::json const &, SchemaField &);
template <> nlohmann::json to_json(EntryPoint const &);
template <> void from_json(nlohmann::json const &, EntryPoint &);
template <> nlohmann::json to_json(PythonImport const &);
template <> void from_json(nlohmann::json const &, PythonImport &);
template <> nlohmann::json to_json(Prompt const &);
template <> void from_json(nlohmann::json const &, Prompt &);

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

nlohmann::json sv_to_json(autocog::types::Value const & v) {
  return std::visit(overloaded{
    [](int x) -> nlohmann::json { return x; },
    [](float x) -> nlohmann::json { return x; },
    [](bool x) -> nlohmann::json { return x; },
    [](std::string const & x) -> nlohmann::json { return x; },
    [](std::monostate) -> nlohmann::json { return nullptr; },
  }, v);
}
autocog::types::Value sv_from_json(nlohmann::json const & j) {
  if (j.is_number_integer()) return j.get<int>();
  if (j.is_number_float())   return j.get<float>();
  if (j.is_boolean())        return j.get<bool>();
  if (j.is_string())         return j.get<std::string>();
  return std::monostate{};
}

}  // namespace

template <>
nlohmann::json to_json(SearchParams const & sp) {
  nlohmann::json sj = nlohmann::json::object();
  for (auto const & [cat, params] : sp.categories) {
    nlohmann::json pj = nlohmann::json::object();
    for (auto const & [key, val] : params) pj[key] = sv_to_json(val);
    sj[cat] = pj;
  }
  return sj;
}
template <>
void from_json(nlohmann::json const & dom, SearchParams & sp) {
  autocog::codec::read_guarded("SearchParams", [&]{
  for (auto const & [cat, params] : dom.items())
    for (auto const & [key, val] : params.items())
      sp.categories[cat][key] = sv_from_json(val);
  });
}

template <>
nlohmann::json to_json(FieldFormat const & ff) {
  return std::visit(overloaded{
    [](std::monostate) -> nlohmann::json { return {{"type","record"}}; },
    [](CompletionFormat const & f) -> nlohmann::json {
      nlohmann::json j = {{"type","completion"}};
      if (f.length) j["length"] = *f.length;
      if (f.vocab)  j["vocab"]  = *f.vocab;
      return j;
    },
    [](EnumFormat const & f) -> nlohmann::json { return {{"type","enum"}, {"values", f.values}}; },
    [](ChoiceFormat const & f) -> nlohmann::json {
      return {{"type","choice"}, {"mode", f.mode}, {"path", paths_to_json(f.path)}};
    },
  }, ff.value);
}
template <>
void from_json(nlohmann::json const & dom, FieldFormat & out) {
  autocog::codec::read_guarded("FieldFormat", [&]{
  std::string type = dom.at("type").get<std::string>();
  if (type == "record") {
    out.value = std::monostate{};
  } else if (type == "completion") {
    auto & f = out.value.emplace<CompletionFormat>();
    if (dom.contains("length")) f.length = dom["length"].get<int>();
    if (dom.contains("vocab") && !dom["vocab"].is_null()) f.vocab = dom["vocab"].get<std::string>();
  } else if (type == "enum") {
    auto & f = out.value.emplace<EnumFormat>();
    for (auto const & v : dom.at("values")) f.values.push_back(v.get<std::string>());
  } else if (type == "choice") {
    auto & f = out.value.emplace<ChoiceFormat>();
    f.mode = dom.at("mode").get<std::string>();
    paths_from_json(dom.at("path"), f.path);
  } else {
    throw autocog::SchemaError("autocog::data: unknown field format '" + type + "'", type);
  }
  });
}

template <>
nlohmann::json to_json(Flow const & fl) {
  return std::visit(overloaded{
    [](FlowControl const & f) -> nlohmann::json {
      nlohmann::json j = {{"type","control"}, {"prompt", f.prompt}};
      if (f.limit) j["limit"] = *f.limit;
      return j;
    },
    [](FlowReturn const & f) -> nlohmann::json {
      nlohmann::json fields = nlohmann::json::array();
      for (auto const & rf : f.fields) {
        nlohmann::json rj;
        rj["alias"] = rf.alias;
        rj["path"]  = paths_to_json(rf.path);
        fields.push_back(rj);
      }
      return {{"type","return"}, {"fields", fields}};
    },
  }, fl.value);
}
template <>
void from_json(nlohmann::json const & dom, Flow & out) {
  autocog::codec::read_guarded("Flow", [&]{
  std::string type = dom.at("type").get<std::string>();
  if (type == "control") {
    auto & f = out.value.emplace<FlowControl>();
    f.prompt = dom.at("prompt").get<std::string>();
    if (dom.contains("limit")) f.limit = dom["limit"].get<int>();
  } else if (type == "return") {
    auto & f = out.value.emplace<FlowReturn>();
    for (auto const & rj : dom.at("fields")) {
      f.fields.emplace_back();
      ReturnField & rf = f.fields.back();
      rf.alias = rj.at("alias").get<std::string>();
      paths_from_json(rj.at("path"), rf.path);
    }
  } else {
    throw autocog::SchemaError("autocog::data: unknown flow type '" + type + "'", type);
  }
  });
}

template <>
nlohmann::json to_json(Field const & fd) {
  nlohmann::json j;
  j["name"] = fd.name; j["depth"] = fd.depth; j["index"] = fd.index; j["flat_index"] = fd.flat_index;
  j["desc"] = fd.desc;
  j["range"] = fd.range ? nlohmann::json::array({fd.range->first, fd.range->second}) : nlohmann::json(nullptr);
  j["format"] = to_json(fd.format);
  if (fd.format_ref) j["format_ref"] = *fd.format_ref;
  if (!fd.format_desc.empty()) j["format_desc"] = fd.format_desc;
  if (!fd.search.empty()) j["search"] = to_json(fd.search);
  return j;
}
template <>
void from_json(nlohmann::json const & dom, Field & f) {
  autocog::codec::read_guarded("Field", [&]{
  f.name = dom.at("name").get<std::string>();
  f.depth = dom.at("depth").get<int>();
  f.index = dom.at("index").get<int>();
  f.flat_index = dom.at("flat_index").get<int>();
  f.desc = dom.at("desc").get<std::vector<std::string>>();
  if (dom.contains("range") && !dom["range"].is_null())
    f.range = std::make_pair(dom["range"].at(0).get<int>(), dom["range"].at(1).get<int>());
  from_json(dom.at("format"), f.format);
  if (dom.contains("format_ref"))  f.format_ref  = dom["format_ref"].get<std::string>();
  if (dom.contains("format_desc")) f.format_desc = dom["format_desc"].get<std::vector<std::string>>();
  if (dom.contains("search"))      from_json(dom["search"], f.search);
  });
}

template <>
nlohmann::json to_json(SchemaField const & sf) {
  nlohmann::json j;
  j["type"] = sf.type;
  j["required"] = sf.required;
  if (sf.type == "array") {
    nlohmann::json items;
    items["type"] = sf.items_type.empty() ? std::string("text") : sf.items_type;
    if (sf.items_max_length) items["max_length"] = *sf.items_max_length;
    j["items"] = items;
    if (sf.length)    j["length"]    = *sf.length;
    if (sf.min_items) j["min_items"] = *sf.min_items;
    if (sf.max_items) j["max_items"] = *sf.max_items;
  } else {
    if (sf.max_length) j["max_length"] = *sf.max_length;
    if (!sf.enum_values.empty()) j["enum"] = sf.enum_values;
  }
  return j;
}
template <>
void from_json(nlohmann::json const & dom, SchemaField & s) {
  autocog::codec::read_guarded("SchemaField", [&]{
  s.type = dom.at("type").get<std::string>();
  s.required = dom.value("required", true);
  if (s.type == "array") {
    if (dom.contains("items")) {
      auto const & it = dom["items"];
      s.items_type = it.value("type", std::string{});
      if (it.contains("max_length")) s.items_max_length = it["max_length"].get<int>();
    }
    if (dom.contains("length"))    s.length    = dom["length"].get<int>();
    if (dom.contains("min_items")) s.min_items = dom["min_items"].get<int>();
    if (dom.contains("max_items")) s.max_items = dom["max_items"].get<int>();
  } else {
    if (dom.contains("max_length")) s.max_length = dom["max_length"].get<int>();
    if (dom.contains("enum")) s.enum_values = dom["enum"].get<std::vector<std::string>>();
  }
  });
}

template <>
nlohmann::json to_json(EntryPoint const & e) {
  nlohmann::json j;
  j["prompt"] = e.prompt;
  nlohmann::json in = nlohmann::json::object();
  for (auto const & [k, sf] : e.inputs) in[k] = to_json(sf);
  j["inputs"] = in;
  nlohmann::json out = nlohmann::json::object();
  for (auto const & [k, sf] : e.outputs) out[k] = to_json(sf);
  j["outputs"] = out;
  return j;
}
template <>
void from_json(nlohmann::json const & dom, EntryPoint & e) {
  autocog::codec::read_guarded("EntryPoint", [&]{
  e.prompt = dom.at("prompt").get<std::string>();
  if (dom.contains("inputs"))
    for (auto const & [k, sf] : dom["inputs"].items()) from_json(sf, e.inputs[k]);
  if (dom.contains("outputs"))
    for (auto const & [k, sf] : dom["outputs"].items()) from_json(sf, e.outputs[k]);
  });
}

template <>
nlohmann::json to_json(PythonImport const & p) { return {{"file", p.file}, {"target", p.target}}; }
template <>
void from_json(nlohmann::json const & dom, PythonImport & p) {
  autocog::codec::read_guarded("PythonImport", [&]{
  p.file = dom.at("file").get<std::string>();
  p.target = dom.at("target").get<std::string>();
  });
}

template <>
nlohmann::json to_json(Prompt const & p) {
  nlohmann::json j;
  j["name"] = p.name;
  j["desc"] = p.desc;
  nlohmann::json farr = nlohmann::json::array();
  for (auto const & f : p.fields) farr.push_back(to_json(f));
  j["fields"] = farr;
  nlohmann::json aarr = nlohmann::json::array();
  for (auto const & a : p.abstracts) {
    nlohmann::json aj; aj["field"] = a.field; aj["flow"] = a.flow; aj["exit"] = a.exit_;
    aarr.push_back(aj);
  }
  j["abstracts"] = aarr;
  nlohmann::json fl = nlohmann::json::object();
  for (auto const & [k, flow] : p.flows) fl[k] = to_json(flow);
  j["flows"] = fl;
  nlohmann::json carr = nlohmann::json::array();
  for (auto const & c : p.channels) carr.push_back(to_json(c));
  j["channels"] = carr;
  if (!p.search.empty()) j["search"] = to_json(p.search);
  if (!p.vocabs.empty()) {
    nlohmann::json vj = nlohmann::json::object();
    for (auto const & [k, ve] : p.vocabs) vj[k] = to_json(ve);
    j["vocabs"] = vj;
  }
  return j;
}
template <>
void from_json(nlohmann::json const & dom, Prompt & p) {
  autocog::codec::read_guarded("Prompt", [&]{
  p.name = dom.at("name").get<std::string>();
  p.desc = dom.at("desc").get<std::vector<std::string>>();
  for (auto const & fj : dom.at("fields")) { p.fields.emplace_back(); from_json(fj, p.fields.back()); }
  for (auto const & aj : dom.at("abstracts")) {
    p.abstracts.emplace_back();
    Abstract & a = p.abstracts.back();
    a.field = aj.value("field", -1);
    a.flow  = aj.value("flow", -1);
    a.exit_ = aj.value("exit", -1);
  }
  for (auto const & [k, flj] : dom.at("flows").items()) from_json(flj, p.flows[k]);
  for (auto const & cj : dom.at("channels")) { p.channels.emplace_back(); from_json(cj, p.channels.back()); }
  if (dom.contains("search")) from_json(dom["search"], p.search);
  if (dom.contains("vocabs"))
    for (auto const & [k, vj] : dom["vocabs"].items()) from_json(vj, p.vocabs[k]);
  });
}

template <>
nlohmann::json to_json(STA const & s) {
  nlohmann::json j;
  nlohmann::json ep = nlohmann::json::object();
  for (auto const & [k, e] : s.entry_points) ep[k] = to_json(e);
  j["entry_points"] = ep;
  nlohmann::json pi = nlohmann::json::object();
  for (auto const & [k, imp] : s.python_imports) pi[k] = to_json(imp);
  j["python_imports"] = pi;
  nlohmann::json pr = nlohmann::json::object();
  for (auto const & [k, p] : s.prompts) pr[k] = to_json(p);
  j["prompts"] = pr;
  if (s.metadata) j["metadata"] = to_json(*s.metadata);
  j["provenance"] = s.provenance;
  return j;
}
template <>
void from_json(nlohmann::json const & dom, STA & s) {
  autocog::codec::read_guarded("STA", [&]{
  if (dom.contains("entry_points"))
    for (auto const & [k, ej] : dom["entry_points"].items()) from_json(ej, s.entry_points[k]);
  if (dom.contains("python_imports"))
    for (auto const & [k, ij] : dom["python_imports"].items()) from_json(ij, s.python_imports[k]);
  if (dom.contains("prompts"))
    for (auto const & [k, pj] : dom["prompts"].items()) from_json(pj, s.prompts[k]);
  if (dom.contains("metadata")) { s.metadata.emplace(); from_json(dom.at("metadata"), *s.metadata); }
  if (dom.contains("provenance"))
    s.provenance = dom.at("provenance").get<std::map<std::string, std::string>>();
  });
}

}
