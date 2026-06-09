#include "autocog/codec/python.hxx"

namespace autocog::codec {
using namespace autocog::data;

// File-local sub-struct conversions.
template <>
pybind11::object to_py(TextSearch const & t) {
  namespace py = pybind11;
  py::dict d;
  d["threshold"] = t.threshold;
  d["beams"]     = t.beams;
  d["ahead"]     = t.ahead;
  d["width"]     = t.width;
  if (t.repetition) d["repetition"] = *t.repetition;
  if (t.diversity)  d["diversity"]  = *t.diversity;
  return d;
}
template <>
void from_py(pybind11::object const & obj, TextSearch & out) {
  namespace py = pybind11;
  py::dict t = obj.cast<py::dict>();
  out.threshold = t["threshold"].cast<float>();
  out.beams     = t["beams"].cast<unsigned>();
  out.ahead     = t["ahead"].cast<unsigned>();
  out.width     = t["width"].cast<unsigned>();
  if (t.contains("repetition")) out.repetition = t["repetition"].cast<float>();
  if (t.contains("diversity"))  out.diversity  = t["diversity"].cast<float>();
}

template <>
pybind11::object to_py(ChoiceSearch const & c) {
  namespace py = pybind11;
  py::dict d;
  d["threshold"] = c.threshold;
  d["width"]     = c.width;
  return d;
}
template <>
void from_py(pybind11::object const & obj, ChoiceSearch & out) {
  namespace py = pybind11;
  py::dict c = obj.cast<py::dict>();
  out.threshold = c["threshold"].cast<float>();
  out.width     = c["width"].cast<unsigned>();
}

template <>
pybind11::object to_py(QueueSearch const & q) {
  namespace py = pybind11;
  py::dict d;
  d["metric"] = q.metric;
  return d;
}
template <>
void from_py(pybind11::object const & obj, QueueSearch & out) {
  namespace py = pybind11;
  py::dict q = obj.cast<py::dict>();
  out.metric = q["metric"].cast<std::string>();
}

template <>
pybind11::object to_py(SearchConfig const & s) {
  namespace py = pybind11;
  py::dict d;
  d["text"]   = to_py(s.text);
  d["enum"]   = to_py(s.enums);
  d["branch"] = to_py(s.branch);
  d["flow"]   = to_py(s.flow);
  d["queue"]  = to_py(s.queue);
  if (s.metadata) d["metadata"] = to_py(*s.metadata);
  d["provenance"] = s.provenance;
  return d;
}
template <>
void from_py(pybind11::object const & obj, SearchConfig & s) {
  namespace py = pybind11;
  py::dict d = obj.cast<py::dict>();
  from_py(d["text"],   s.text);
  from_py(d["enum"],   s.enums);
  from_py(d["branch"], s.branch);
  from_py(d["flow"],   s.flow);
  from_py(d["queue"],  s.queue);
  if (d.contains("metadata")) { s.metadata.emplace(); from_py(d["metadata"], *s.metadata); }
  if (d.contains("provenance"))
    s.provenance = d["provenance"].cast<std::map<std::string, std::string>>();
}

}
