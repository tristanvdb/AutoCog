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
  if (t.topk) d["topk"] = *t.topk;
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
  if (t.contains("topk") && !t["topk"].is_none()) out.topk = t["topk"].cast<unsigned>();
  out.ahead     = t["ahead"].cast<unsigned>();
  out.width     = t["width"].cast<unsigned>();
  if (t.contains("repetition")) out.repetition = t["repetition"].cast<float>();
  if (t.contains("diversity"))  out.diversity  = t["diversity"].cast<float>();
}

template <>
pybind11::object to_py(ChoiceSearch const & c) {
  namespace py = pybind11;
  py::dict d;
  if (c.threshold_metric != "mean") {
    py::dict th;
    th["value"]  = c.threshold;
    th["metric"] = c.threshold_metric;
    d["threshold"] = th;
  } else {
    d["threshold"] = c.threshold;
  }
  d["width"] = c.width;
  if (c.ranking != "mean") {
    py::dict r;
    r["metric"] = c.ranking;
    d["ranking"] = r;
  }
  return d;
}
template <>
void from_py(pybind11::object const & obj, ChoiceSearch & out) {
  namespace py = pybind11;
  py::dict c = obj.cast<py::dict>();
  py::object th = c["threshold"];
  if (py::isinstance<py::dict>(th)) {
    py::dict td = th.cast<py::dict>();
    out.threshold = td["value"].cast<float>();
    if (td.contains("metric") && !td["metric"].is_none())
      out.threshold_metric = td["metric"].cast<std::string>();
  } else {
    out.threshold = th.cast<float>();
  }
  out.width = c["width"].cast<unsigned>();
  if (c.contains("ranking") && !c["ranking"].is_none()) {
    py::object r = c["ranking"];
    out.ranking = py::isinstance<py::dict>(r)
        ? r.cast<py::dict>()["metric"].cast<std::string>()
        : r.cast<std::string>();
  }
}

template <>
pybind11::object to_py(QueueSearch const & q) {
  namespace py = pybind11;
  py::dict d;
  d["metric"] = q.metric;
  if (q.stop) d["stop"] = to_py(*q.stop);
  return d;
}
template <>
void from_py(pybind11::object const & obj, QueueSearch & out) {
  namespace py = pybind11;
  py::dict q = obj.cast<py::dict>();
  // A single metric may be given as a bare string; a list is lexicographic.
  out.metric.clear();
  if (py::isinstance<py::str>(q["metric"])) out.metric.push_back(q["metric"].cast<std::string>());
  else out.metric = q["metric"].cast<std::vector<std::string>>();
  if (q.contains("stop") && !q["stop"].is_none()) {
    out.stop.emplace();
    from_py(py::reinterpret_borrow<py::object>(q["stop"]), *out.stop);
  }
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
