#include "autocog/codec/python.hxx"

#include "autocog/utilities/errors.hxx"

namespace autocog::codec {
using namespace autocog::data;

namespace {
template <class... Ts> struct overloaded : Ts... { using Ts::operator()...; };
template <class... Ts> overloaded(Ts...) -> overloaded<Ts...>;
}  // namespace

// File-local Action conversion (Action is FTA's node, used only here).
template <>
pybind11::object to_py(Action const & act) {
  namespace py = pybind11;
  py::dict d;
  d["uid"] = act.uid;
  std::visit(overloaded{
    [&](TextAction const & a) {
      d["type"] = "text";
      d["text"] = a.text;
      if (a.evaluate) d["evaluate"] = a.evaluate;
    },
    [&](CompleteAction const & a) {
      d["type"]      = "complete";
      d["length"]    = a.length;
      d["threshold"] = a.threshold;
      d["beams"]     = a.beams;
      d["ahead"]     = a.ahead;
      d["width"]     = a.width;
      d["stop_text"] = a.stop_text;
      if (a.repetition) d["repetition"] = *a.repetition;
      if (a.diversity)  d["diversity"]  = *a.diversity;
      if (a.vocab)      d["vocab"]      = *a.vocab;
    },
    [&](ChooseAction const & a) {
      d["type"]      = "choose";
      d["choices"]   = a.choices;
      d["threshold"] = a.threshold;
      d["width"]     = a.width;
    },
  }, act.body);
  if (act.field)   d["field"]   = *act.field;
  if (act.indices) d["indices"] = *act.indices;
  d["successors"] = act.successors;
  return d;
}

template <>
void from_py(pybind11::object const & obj, Action & a) {
  namespace py = pybind11;
  py::dict d = obj.cast<py::dict>();
  a.uid = d["uid"].cast<std::string>();
  std::string type = d["type"].cast<std::string>();
  if (type == "text") {
    auto & t = a.body.emplace<TextAction>();
    if (d.contains("text"))     t.text     = d["text"].cast<std::string>();
    if (d.contains("evaluate")) t.evaluate = d["evaluate"].cast<bool>();
  } else if (type == "complete") {
    auto & c = a.body.emplace<CompleteAction>();
    c.length    = d["length"].cast<unsigned>();
    c.threshold = d["threshold"].cast<float>();
    c.beams     = d["beams"].cast<unsigned>();
    c.ahead     = d["ahead"].cast<unsigned>();
    c.width     = d["width"].cast<unsigned>();
    c.stop_text = d["stop_text"].cast<std::string>();
    if (d.contains("repetition") && !d["repetition"].is_none()) c.repetition = d["repetition"].cast<float>();
    if (d.contains("diversity")  && !d["diversity"].is_none())  c.diversity  = d["diversity"].cast<float>();
    if (d.contains("vocab")      && !d["vocab"].is_none())      c.vocab      = d["vocab"].cast<std::string>();
  } else if (type == "choose") {
    auto & ch = a.body.emplace<ChooseAction>();
    if (d.contains("choices")) ch.choices = d["choices"].cast<std::vector<std::string>>();
    ch.threshold = d["threshold"].cast<float>();
    ch.width     = d["width"].cast<unsigned>();
  } else {
    throw autocog::SchemaError("autocog::data: unknown action type '" + type + "'", type);
  }
  if (d.contains("field"))      a.field      = d["field"].cast<int>();
  if (d.contains("indices"))    a.indices    = d["indices"].cast<std::vector<int>>();
  if (d.contains("successors")) a.successors = d["successors"].cast<std::vector<std::string>>();
}

template <>
pybind11::object to_py(FTA const & s) {
  namespace py = pybind11;
  py::dict d;
  py::list arr;
  for (auto const & a : s.actions) arr.append(to_py(a));
  d["actions"] = arr;
  py::dict q;
  q["metric"] = s.queue_metric;
  d["queue"] = q;
  if (!s.vocabs.empty()) {
    py::dict vj;
    for (auto const & [k, ve] : s.vocabs) vj[py::str(k)] = to_py(ve);
    d["vocabs"] = vj;
  }
  if (s.metadata) d["metadata"] = to_py(*s.metadata);
  d["provenance"] = s.provenance;
  return d;
}

template <>
void from_py(pybind11::object const & obj, FTA & s) {
  namespace py = pybind11;
  py::dict d = obj.cast<py::dict>();
  for (auto const & a : d["actions"].cast<py::list>()) {
    s.actions.emplace_back();
    from_py(py::reinterpret_borrow<py::object>(a), s.actions.back());
  }
  if (d.contains("queue")) {
    py::dict q = d["queue"].cast<py::dict>();
    if (q.contains("metric")) s.queue_metric = q["metric"].cast<std::string>();
  }
  if (d.contains("vocabs")) {
    py::dict vj = d["vocabs"].cast<py::dict>();
    for (auto const & item : vj)
      from_py(py::reinterpret_borrow<py::object>(item.second), s.vocabs[item.first.cast<std::string>()]);
  }
  if (d.contains("metadata")) { s.metadata.emplace(); from_py(d["metadata"], *s.metadata); }
  if (d.contains("provenance"))
    s.provenance = d["provenance"].cast<std::map<std::string, std::string>>();
}

}
