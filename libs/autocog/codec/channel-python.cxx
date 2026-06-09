#include "autocog/codec/python.hxx"

#include "autocog/utilities/errors.hxx"

namespace autocog::codec {
using namespace autocog::data;

template <> pybind11::object to_py(Clause const &);
template <> void from_py(pybind11::object const &, Clause &);
template <> pybind11::object to_py(ChannelKwarg const &);
template <> void from_py(pybind11::object const &, ChannelKwarg &);

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
py::list clauses_to_py(std::vector<Clause> const & v) {
  py::list l;
  for (auto const & c : v) l.append(to_py(c));
  return l;
}
void clauses_from_py(py::object const & obj, std::vector<Clause> & out) {
  for (auto const & e : obj.cast<py::list>()) { out.emplace_back(); from_py(py::reinterpret_borrow<py::object>(e), out.back()); }
}

}  // namespace

template <>
pybind11::object to_py(Clause const & c) {
  return std::visit(overloaded{
    [](BindClause const & x) -> py::object {
      py::dict d; d["type"]="bind"; d["source"]=paths_to_py(x.source); d["target"]=paths_to_py(x.target); return d;
    },
    [](RavelClause const & x) -> py::object {
      py::dict d; d["type"]="ravel"; d["target"]=paths_to_py(x.target); if (x.depth) d["depth"]=*x.depth; return d;
    },
    [](WrapClause const & x) -> py::object {
      py::dict d; d["type"]="wrap"; d["target"]=paths_to_py(x.target); return d;
    },
    [](PruneClause const & x) -> py::object {
      py::dict d; d["type"]="prune"; d["target"]=paths_to_py(x.target); return d;
    },
    [](MappedClause const & x) -> py::object {
      py::dict d; d["type"]="mapped"; d["target"]=paths_to_py(x.target); return d;
    },
  }, c.value);
}

template <>
void from_py(pybind11::object const & obj, Clause & out) {
  py::dict d = obj.cast<py::dict>();
  std::string type = d["type"].cast<std::string>();
  if (type == "bind") {
    auto & x = out.value.emplace<BindClause>(); paths_from_py(d["source"], x.source); paths_from_py(d["target"], x.target);
  } else if (type == "ravel") {
    auto & x = out.value.emplace<RavelClause>(); if (d.contains("depth")) x.depth = d["depth"].cast<int>(); paths_from_py(d["target"], x.target);
  } else if (type == "wrap") {
    auto & x = out.value.emplace<WrapClause>(); paths_from_py(d["target"], x.target);
  } else if (type == "prune") {
    auto & x = out.value.emplace<PruneClause>(); paths_from_py(d["target"], x.target);
  } else if (type == "mapped") {
    auto & x = out.value.emplace<MappedClause>(); paths_from_py(d["target"], x.target);
  } else {
    throw autocog::SchemaError("autocog::data: unknown clause type '" + type + "'", type);
  }
}

template <>
pybind11::object to_py(ChannelKwarg const & k) {
  py::dict d;
  d["name"]     = k.name;
  d["is_input"] = k.is_input;
  d["path"]     = paths_to_py(k.path);
  d["clauses"]  = clauses_to_py(k.clauses);
  d["prompt"]   = k.prompt ? py::cast(*k.prompt) : py::none();
  d["value"]    = k.value  ? py::cast(*k.value)  : py::none();
  return d;
}

template <>
void from_py(pybind11::object const & obj, ChannelKwarg & k) {
  py::dict d = obj.cast<py::dict>();
  k.name     = d["name"].cast<std::string>();
  k.is_input = d["is_input"].cast<bool>();
  paths_from_py(d["path"], k.path);
  clauses_from_py(d["clauses"], k.clauses);
  if (d.contains("prompt") && !d["prompt"].is_none()) k.prompt = d["prompt"].cast<std::string>();
  if (d.contains("value")  && !d["value"].is_none())  k.value  = d["value"].cast<std::string>();
}

template <>
pybind11::object to_py(Channel const & c) {
  return std::visit(overloaded{
    [](InputChannel const & x) -> py::object {
      py::dict d; d["type"]="input"; d["target"]=paths_to_py(x.target); d["source"]=paths_to_py(x.source); return d;
    },
    [](DataflowChannel const & x) -> py::object {
      py::dict d; d["type"]="dataflow"; d["target"]=paths_to_py(x.target); d["source"]=paths_to_py(x.source);
      d["clauses"]=clauses_to_py(x.clauses); d["prompt"]= x.prompt ? py::cast(*x.prompt) : py::none(); return d;
    },
    [](CallChannel const & x) -> py::object {
      py::dict d; d["type"]="call"; d["target"]=paths_to_py(x.target);
      py::list kwargs; for (auto const & kw : x.kwargs) kwargs.append(to_py(kw));
      d["kwargs"]=kwargs; d["clauses"]=clauses_to_py(x.clauses);
      d["extern"]= x.extern_func ? py::cast(*x.extern_func) : py::none();
      d["entry"] = x.entry ? py::cast(*x.entry) : py::none();
      return d;
    },
  }, c.value);
}

template <>
void from_py(pybind11::object const & obj, Channel & out) {
  py::dict d = obj.cast<py::dict>();
  std::string type = d["type"].cast<std::string>();
  if (type == "input") {
    auto & x = out.value.emplace<InputChannel>(); paths_from_py(d["target"], x.target); paths_from_py(d["source"], x.source);
  } else if (type == "dataflow") {
    auto & x = out.value.emplace<DataflowChannel>();
    paths_from_py(d["target"], x.target); paths_from_py(d["source"], x.source); clauses_from_py(d["clauses"], x.clauses);
    if (d.contains("prompt") && !d["prompt"].is_none()) x.prompt = d["prompt"].cast<std::string>();
  } else if (type == "call") {
    auto & x = out.value.emplace<CallChannel>();
    paths_from_py(d["target"], x.target); clauses_from_py(d["clauses"], x.clauses);
    for (auto const & kj : d["kwargs"].cast<py::list>()) { x.kwargs.emplace_back(); from_py(py::reinterpret_borrow<py::object>(kj), x.kwargs.back()); }
    if (d.contains("extern") && !d["extern"].is_none()) x.extern_func = d["extern"].cast<std::string>();
    if (d.contains("entry")  && !d["entry"].is_none())  x.entry       = d["entry"].cast<std::string>();
  } else {
    throw autocog::SchemaError("autocog::data: unknown channel type '" + type + "'", type);
  }
}

}
