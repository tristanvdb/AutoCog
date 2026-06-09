#include "autocog/codec/python.hxx"

#include "autocog/utilities/errors.hxx"

namespace autocog::codec {
using namespace autocog::data;

template <> pybind11::object to_py(SearchParams const &);
template <> void from_py(pybind11::object const &, SearchParams &);
template <> pybind11::object to_py(FieldFormat const &);
template <> void from_py(pybind11::object const &, FieldFormat &);
template <> pybind11::object to_py(Flow const &);
template <> void from_py(pybind11::object const &, Flow &);
template <> pybind11::object to_py(Field const &);
template <> void from_py(pybind11::object const &, Field &);
template <> pybind11::object to_py(SchemaField const &);
template <> void from_py(pybind11::object const &, SchemaField &);
template <> pybind11::object to_py(EntryPoint const &);
template <> void from_py(pybind11::object const &, EntryPoint &);
template <> pybind11::object to_py(PythonImport const &);
template <> void from_py(pybind11::object const &, PythonImport &);
template <> pybind11::object to_py(Prompt const &);
template <> void from_py(pybind11::object const &, Prompt &);

namespace {

namespace py = pybind11;

template <class... Ts> struct overloaded : Ts... { using Ts::operator()...; };
template <class... Ts> overloaded(Ts...) -> overloaded<Ts...>;

py::list paths_to_py(std::vector<PathStep> const & v) {
  py::list l;
  for (auto const & p : v) l.append(to_py(p));
  return l;
}
void paths_from_py(py::object const & obj, std::vector<PathStep> & out) {
  for (auto const & e : obj.cast<py::list>()) { out.emplace_back(); from_py(py::reinterpret_borrow<py::object>(e), out.back()); }
}

py::object sv_to_py(autocog::types::Value const & v) {
  return std::visit(overloaded{
    [](int x) -> py::object { return py::cast(x); },
    [](float x) -> py::object { return py::cast(x); },
    [](bool x) -> py::object { return py::cast(x); },
    [](std::string const & x) -> py::object { return py::cast(x); },
    [](std::monostate) -> py::object { return py::none(); },
  }, v);
}
autocog::types::Value sv_from_py(py::handle h) {
  if (py::isinstance<py::bool_>(h))  return h.cast<bool>();   // bool before int
  if (py::isinstance<py::int_>(h))   return h.cast<int>();
  if (py::isinstance<py::float_>(h)) return h.cast<float>();
  if (py::isinstance<py::str>(h))    return h.cast<std::string>();
  return std::monostate{};
}

}  // namespace

template <>
pybind11::object to_py(SearchParams const & sp) {
  py::dict d;
  for (auto const & [cat, params] : sp.categories) {
    py::dict pj;
    for (auto const & [key, val] : params) pj[py::str(key)] = sv_to_py(val);
    d[py::str(cat)] = pj;
  }
  return d;
}
template <>
void from_py(pybind11::object const & obj, SearchParams & sp) {
  py::dict d = obj.cast<py::dict>();
  for (auto const & cat_item : d) {
    std::string cat = cat_item.first.cast<std::string>();
    for (auto const & p : cat_item.second.cast<py::dict>())
      sp.categories[cat][p.first.cast<std::string>()] = sv_from_py(p.second);
  }
}

template <>
pybind11::object to_py(FieldFormat const & ff) {
  return std::visit(overloaded{
    [](std::monostate) -> py::object { py::dict d; d["type"]="record"; return d; },
    [](CompletionFormat const & f) -> py::object {
      py::dict d; d["type"]="completion";
      if (f.length) d["length"]=*f.length;
      if (f.vocab)  d["vocab"]=*f.vocab;
      return d;
    },
    [](EnumFormat const & f) -> py::object { py::dict d; d["type"]="enum"; d["values"]=f.values; return d; },
    [](ChoiceFormat const & f) -> py::object {
      py::dict d; d["type"]="choice"; d["mode"]=f.mode; d["path"]=paths_to_py(f.path); return d;
    },
  }, ff.value);
}
template <>
void from_py(pybind11::object const & obj, FieldFormat & out) {
  py::dict d = obj.cast<py::dict>();
  std::string type = d["type"].cast<std::string>();
  if (type == "record") {
    out.value = std::monostate{};
  } else if (type == "completion") {
    auto & f = out.value.emplace<CompletionFormat>();
    if (d.contains("length")) f.length = d["length"].cast<int>();
    if (d.contains("vocab") && !d["vocab"].is_none()) f.vocab = d["vocab"].cast<std::string>();
  } else if (type == "enum") {
    auto & f = out.value.emplace<EnumFormat>();
    f.values = d["values"].cast<std::vector<std::string>>();
  } else if (type == "choice") {
    auto & f = out.value.emplace<ChoiceFormat>();
    f.mode = d["mode"].cast<std::string>();
    paths_from_py(d["path"], f.path);
  } else {
    throw autocog::SchemaError("autocog::data: unknown field format '" + type + "'", type);
  }
}

template <>
pybind11::object to_py(Flow const & fl) {
  return std::visit(overloaded{
    [](FlowControl const & f) -> py::object {
      py::dict d; d["type"]="control"; d["prompt"]=f.prompt; if (f.limit) d["limit"]=*f.limit; return d;
    },
    [](FlowReturn const & f) -> py::object {
      py::dict d; d["type"]="return";
      py::list fields;
      for (auto const & rf : f.fields) { py::dict rj; rj["alias"]=rf.alias; rj["path"]=paths_to_py(rf.path); fields.append(rj); }
      d["fields"]=fields;
      return d;
    },
  }, fl.value);
}
template <>
void from_py(pybind11::object const & obj, Flow & out) {
  py::dict d = obj.cast<py::dict>();
  std::string type = d["type"].cast<std::string>();
  if (type == "control") {
    auto & f = out.value.emplace<FlowControl>();
    f.prompt = d["prompt"].cast<std::string>();
    if (d.contains("limit")) f.limit = d["limit"].cast<int>();
  } else if (type == "return") {
    auto & f = out.value.emplace<FlowReturn>();
    for (auto const & rj : d["fields"].cast<py::list>()) {
      py::dict rd = py::reinterpret_borrow<py::object>(rj).cast<py::dict>();
      f.fields.emplace_back();
      ReturnField & rf = f.fields.back();
      rf.alias = rd["alias"].cast<std::string>();
      paths_from_py(rd["path"], rf.path);
    }
  } else {
    throw autocog::SchemaError("autocog::data: unknown flow type '" + type + "'", type);
  }
}

template <>
pybind11::object to_py(Field const & fd) {
  py::dict d;
  d["name"]=fd.name; d["depth"]=fd.depth; d["index"]=fd.index; d["flat_index"]=fd.flat_index; d["desc"]=fd.desc;
  if (fd.range) { py::list r; r.append(fd.range->first); r.append(fd.range->second); d["range"]=r; } else d["range"]=py::none();
  d["format"]=to_py(fd.format);
  if (fd.format_ref) d["format_ref"]=*fd.format_ref;
  if (!fd.format_desc.empty()) d["format_desc"]=fd.format_desc;
  if (!fd.search.empty()) d["search"]=to_py(fd.search);
  return d;
}
template <>
void from_py(pybind11::object const & obj, Field & f) {
  py::dict d = obj.cast<py::dict>();
  f.name = d["name"].cast<std::string>();
  f.depth = d["depth"].cast<int>();
  f.index = d["index"].cast<int>();
  f.flat_index = d["flat_index"].cast<int>();
  f.desc = d["desc"].cast<std::vector<std::string>>();
  if (d.contains("range") && !d["range"].is_none()) {
    py::list r = d["range"].cast<py::list>();
    f.range = std::make_pair(r[0].cast<int>(), r[1].cast<int>());
  }
  from_py(d["format"], f.format);
  if (d.contains("format_ref"))  f.format_ref  = d["format_ref"].cast<std::string>();
  if (d.contains("format_desc")) f.format_desc = d["format_desc"].cast<std::vector<std::string>>();
  if (d.contains("search"))      from_py(d["search"], f.search);
}

template <>
pybind11::object to_py(SchemaField const & sf) {
  py::dict d;
  d["type"] = sf.type; d["required"] = sf.required;
  if (sf.type == "array") {
    py::dict items; items["type"] = sf.items_type.empty() ? std::string("text") : sf.items_type;
    if (sf.items_max_length) items["max_length"] = *sf.items_max_length;
    d["items"] = items;
    if (sf.length)    d["length"]    = *sf.length;
    if (sf.min_items) d["min_items"] = *sf.min_items;
    if (sf.max_items) d["max_items"] = *sf.max_items;
  } else {
    if (sf.max_length) d["max_length"] = *sf.max_length;
    if (!sf.enum_values.empty()) d["enum"] = sf.enum_values;
  }
  return d;
}
template <>
void from_py(pybind11::object const & obj, SchemaField & s) {
  py::dict d = obj.cast<py::dict>();
  s.type = d["type"].cast<std::string>();
  if (d.contains("required")) s.required = d["required"].cast<bool>();
  if (s.type == "array") {
    if (d.contains("items")) {
      py::dict it = d["items"].cast<py::dict>();
      if (it.contains("type")) s.items_type = it["type"].cast<std::string>();
      if (it.contains("max_length")) s.items_max_length = it["max_length"].cast<int>();
    }
    if (d.contains("length"))    s.length    = d["length"].cast<int>();
    if (d.contains("min_items")) s.min_items = d["min_items"].cast<int>();
    if (d.contains("max_items")) s.max_items = d["max_items"].cast<int>();
  } else {
    if (d.contains("max_length")) s.max_length = d["max_length"].cast<int>();
    if (d.contains("enum")) s.enum_values = d["enum"].cast<std::vector<std::string>>();
  }
}

template <>
pybind11::object to_py(EntryPoint const & e) {
  py::dict d;
  d["prompt"] = e.prompt;
  py::dict in;  for (auto const & [k, sf] : e.inputs)  in[py::str(k)]  = to_py(sf);
  py::dict out; for (auto const & [k, sf] : e.outputs) out[py::str(k)] = to_py(sf);
  d["inputs"] = in; d["outputs"] = out;
  return d;
}
template <>
void from_py(pybind11::object const & obj, EntryPoint & e) {
  py::dict d = obj.cast<py::dict>();
  e.prompt = d["prompt"].cast<std::string>();
  if (d.contains("inputs"))
    for (auto const & it : d["inputs"].cast<py::dict>())
      from_py(py::reinterpret_borrow<py::object>(it.second), e.inputs[it.first.cast<std::string>()]);
  if (d.contains("outputs"))
    for (auto const & it : d["outputs"].cast<py::dict>())
      from_py(py::reinterpret_borrow<py::object>(it.second), e.outputs[it.first.cast<std::string>()]);
}

template <>
pybind11::object to_py(PythonImport const & p) {
  py::dict d; d["file"] = p.file; d["target"] = p.target; return d;
}
template <>
void from_py(pybind11::object const & obj, PythonImport & p) {
  py::dict d = obj.cast<py::dict>();
  p.file = d["file"].cast<std::string>();
  p.target = d["target"].cast<std::string>();
}

template <>
pybind11::object to_py(Prompt const & p) {
  py::dict d;
  d["name"] = p.name; d["desc"] = p.desc;
  py::list farr; for (auto const & f : p.fields) farr.append(to_py(f)); d["fields"] = farr;
  py::list aarr;
  for (auto const & a : p.abstracts) { py::dict aj; aj["field"]=a.field; aj["flow"]=a.flow; aj["exit"]=a.exit_; aarr.append(aj); }
  d["abstracts"] = aarr;
  py::dict fl; for (auto const & [k, flow] : p.flows) fl[py::str(k)] = to_py(flow); d["flows"] = fl;
  py::list carr; for (auto const & c : p.channels) carr.append(to_py(c)); d["channels"] = carr;
  if (!p.search.empty()) d["search"] = to_py(p.search);
  if (!p.vocabs.empty()) { py::dict vj; for (auto const & [k, ve] : p.vocabs) vj[py::str(k)] = to_py(ve); d["vocabs"] = vj; }
  return d;
}
template <>
void from_py(pybind11::object const & obj, Prompt & p) {
  py::dict d = obj.cast<py::dict>();
  p.name = d["name"].cast<std::string>();
  p.desc = d["desc"].cast<std::vector<std::string>>();
  for (auto const & fj : d["fields"].cast<py::list>()) { p.fields.emplace_back(); from_py(py::reinterpret_borrow<py::object>(fj), p.fields.back()); }
  for (auto const & aj : d["abstracts"].cast<py::list>()) {
    py::dict ad = py::reinterpret_borrow<py::object>(aj).cast<py::dict>();
    p.abstracts.emplace_back();
    Abstract & a = p.abstracts.back();
    a.field = ad["field"].cast<int>(); a.flow = ad["flow"].cast<int>(); a.exit_ = ad["exit"].cast<int>();
  }
  for (auto const & it : d["flows"].cast<py::dict>())
    from_py(py::reinterpret_borrow<py::object>(it.second), p.flows[it.first.cast<std::string>()]);
  for (auto const & cj : d["channels"].cast<py::list>()) { p.channels.emplace_back(); from_py(py::reinterpret_borrow<py::object>(cj), p.channels.back()); }
  if (d.contains("search")) from_py(d["search"], p.search);
  if (d.contains("vocabs"))
    for (auto const & it : d["vocabs"].cast<py::dict>())
      from_py(py::reinterpret_borrow<py::object>(it.second), p.vocabs[it.first.cast<std::string>()]);
}

template <>
pybind11::object to_py(STA const & s) {
  py::dict d;
  py::dict ep; for (auto const & [k, e] : s.entry_points) ep[py::str(k)] = to_py(e); d["entry_points"] = ep;
  py::dict pi; for (auto const & [k, imp] : s.python_imports) pi[py::str(k)] = to_py(imp); d["python_imports"] = pi;
  py::dict pr; for (auto const & [k, p] : s.prompts) pr[py::str(k)] = to_py(p); d["prompts"] = pr;
  if (s.metadata) d["metadata"] = to_py(*s.metadata);
  d["provenance"] = s.provenance;
  return d;
}
template <>
void from_py(pybind11::object const & obj, STA & s) {
  py::dict d = obj.cast<py::dict>();
  if (d.contains("entry_points"))
    for (auto const & it : d["entry_points"].cast<py::dict>())
      from_py(py::reinterpret_borrow<py::object>(it.second), s.entry_points[it.first.cast<std::string>()]);
  if (d.contains("python_imports"))
    for (auto const & it : d["python_imports"].cast<py::dict>())
      from_py(py::reinterpret_borrow<py::object>(it.second), s.python_imports[it.first.cast<std::string>()]);
  if (d.contains("prompts"))
    for (auto const & it : d["prompts"].cast<py::dict>())
      from_py(py::reinterpret_borrow<py::object>(it.second), s.prompts[it.first.cast<std::string>()]);
  if (d.contains("metadata")) { s.metadata.emplace(); from_py(d["metadata"], *s.metadata); }
  if (d.contains("provenance"))
    s.provenance = d["provenance"].cast<std::map<std::string, std::string>>();
}

}
