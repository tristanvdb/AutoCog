#include "autocog/codec/python.hxx"

namespace autocog::codec {
using namespace autocog::data;

namespace {
pybind11::object pruned_to_py(Pruned p) {
  namespace py = pybind11;
  switch (p) {
    case Pruned::No:        return py::none();
    case Pruned::Width:     return py::str("width");
    case Pruned::Threshold: return py::str("threshold");
    case Pruned::Abandoned: return py::str("abandoned");
  }
  return py::none();
}
Pruned pruned_from_py(pybind11::object const & o) {
  if (o.is_none()) return Pruned::No;
  auto const s = o.cast<std::string>();
  if (s == "width")     return Pruned::Width;
  if (s == "threshold") return Pruned::Threshold;
  if (s == "abandoned") return Pruned::Abandoned;
  throw autocog::SchemaError("autocog::data: unknown prune reason '" + s + "'", s);
}
}  // namespace

// File-local node conversion (FTTNode is FTT's content, used only here).
template <>
pybind11::object to_py(FTTNode const & n) {
  namespace py = pybind11;
  py::dict d;
  d["action"] = n.action;
  if (n.uid)     d["uid"]     = *n.uid;
  if (n.field)   d["field"]   = *n.field;
  if (n.indices) d["indices"] = *n.indices;
  d["text"]     = n.text;
  d["logprob"]  = n.logprob;
  d["logprobs"] = n.logprobs;
  d["length"]   = n.length;
  d["pruned"]   = pruned_to_py(n.pruned);
  d["tokens"]   = n.tokens;
  py::list kids;
  for (auto const & c : n.children) kids.append(to_py(c));
  d["children"] = kids;
  return d;
}
template <>
void from_py(pybind11::object const & obj, FTTNode & n) {
  namespace py = pybind11;
  py::dict d = obj.cast<py::dict>();
  n.action = d["action"].cast<ActionID>();
  if (d.contains("uid"))     n.uid     = d["uid"].cast<std::string>();
  if (d.contains("field"))   n.field   = d["field"].cast<int>();
  if (d.contains("indices")) n.indices = d["indices"].cast<std::vector<int>>();
  n.text     = d["text"].cast<std::string>();
  n.logprob  = d["logprob"].cast<float>();
  n.logprobs = d["logprobs"].cast<std::vector<float>>();
  n.length   = d["length"].cast<unsigned>();
  n.pruned   = pruned_from_py(py::reinterpret_borrow<py::object>(d["pruned"]));
  n.tokens   = d["tokens"].cast<std::vector<TokenID>>();
  for (auto const & c : d["children"].cast<py::list>()) {
    n.children.emplace_back();
    from_py(py::reinterpret_borrow<py::object>(c), n.children.back());
  }
}

template <>
pybind11::object to_py(FTT const & s) {
  namespace py = pybind11;
  py::object obj = to_py(s.root);
  py::dict d = obj.cast<py::dict>();
  if (s.metadata) d["metadata"] = to_py(*s.metadata);
  d["provenance"] = s.provenance;
  return d;
}
template <>
void from_py(pybind11::object const & obj, FTT & s) {
  from_py(obj, s.root);
  namespace py = pybind11;
  py::dict d = obj.cast<py::dict>();
  if (d.contains("metadata")) { s.metadata.emplace(); from_py(d["metadata"], *s.metadata); }
  if (d.contains("provenance"))
    s.provenance = d["provenance"].cast<std::map<std::string, std::string>>();
}

}
