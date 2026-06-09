#include "autocog/codec/python.hxx"

#include "autocog/utilities/errors.hxx"

namespace autocog::codec {
using namespace autocog::data;

namespace {

char const * kind_str(VocabExpr::Kind k) {
  switch (k) {
    case VocabExpr::Kind::Tokenize:   return "tokenize";
    case VocabExpr::Kind::Regex:      return "regex";
    case VocabExpr::Kind::Union:      return "union";
    case VocabExpr::Kind::Intersect:  return "intersect";
    case VocabExpr::Kind::Diff:       return "diff";
    case VocabExpr::Kind::Complement: return "complement";
  }
  return "";
}

VocabExpr::Kind kind_from_str(std::string const & s) {
  if (s == "tokenize")   return VocabExpr::Kind::Tokenize;
  if (s == "regex")      return VocabExpr::Kind::Regex;
  if (s == "union")      return VocabExpr::Kind::Union;
  if (s == "intersect")  return VocabExpr::Kind::Intersect;
  if (s == "diff")       return VocabExpr::Kind::Diff;
  if (s == "complement") return VocabExpr::Kind::Complement;
  throw autocog::SchemaError("autocog::data: unknown vocab kind '" + s + "'", s);
}

bool is_leaf(VocabExpr::Kind k) {
  return k == VocabExpr::Kind::Tokenize || k == VocabExpr::Kind::Regex;
}

}  // namespace

template <>
pybind11::object to_py(VocabExpr const & ve) {
  namespace py = pybind11;
  py::dict d;
  d["kind"] = kind_str(ve.kind);
  if (is_leaf(ve.kind)) {
    d["strings"] = ve.strings;
  } else {
    py::list ops;
    for (auto const & op : ve.operands) ops.append(to_py(op));
    d["operands"] = ops;
  }
  return d;
}

template <>
void from_py(pybind11::object const & obj, VocabExpr & ve) {
  namespace py = pybind11;
  py::dict d = obj.cast<py::dict>();
  ve.kind = kind_from_str(d["kind"].cast<std::string>());
  if (is_leaf(ve.kind)) {
    if (d.contains("strings")) ve.strings = d["strings"].cast<std::vector<std::string>>();
  } else {
    if (d.contains("operands"))
      for (auto const & o : d["operands"].cast<py::list>()) {
        ve.operands.emplace_back();
        from_py(py::reinterpret_borrow<py::object>(o), ve.operands.back());
      }
  }
}

}
