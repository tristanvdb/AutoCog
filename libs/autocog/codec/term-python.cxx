#include "autocog/codec/python.hxx"

namespace autocog::codec {
using namespace autocog::data;

// Python mirror of the JSON form: a single-key dict —
//   {"all": [<expr>...]}  {"any": [<expr>...]}  {"not": <expr>}
//   {"ge": ["<scalar>", <number>]}  (same shape for gt/le/lt)

namespace {
char const * term_kind_str(TermExpr::Kind k) {
  switch (k) {
    case TermExpr::Kind::All: return "all";
    case TermExpr::Kind::Any: return "any";
    case TermExpr::Kind::Not: return "not";
    case TermExpr::Kind::Ge:  return "ge";
    case TermExpr::Kind::Gt:  return "gt";
    case TermExpr::Kind::Le:  return "le";
    case TermExpr::Kind::Lt:  return "lt";
  }
  return "all";
}

bool term_kind_from_str(std::string const & s, TermExpr::Kind & out) {
  if (s == "all") { out = TermExpr::Kind::All; return true; }
  if (s == "any") { out = TermExpr::Kind::Any; return true; }
  if (s == "not") { out = TermExpr::Kind::Not; return true; }
  if (s == "ge")  { out = TermExpr::Kind::Ge;  return true; }
  if (s == "gt")  { out = TermExpr::Kind::Gt;  return true; }
  if (s == "le")  { out = TermExpr::Kind::Le;  return true; }
  if (s == "lt")  { out = TermExpr::Kind::Lt;  return true; }
  return false;
}

bool term_is_comparison(TermExpr::Kind k) {
  return k == TermExpr::Kind::Ge || k == TermExpr::Kind::Gt
      || k == TermExpr::Kind::Le || k == TermExpr::Kind::Lt;
}
}

template <>
pybind11::object to_py(TermExpr const & e) {
  namespace py = pybind11;
  py::dict d;
  if (term_is_comparison(e.kind)) {
    py::list body;
    body.append(e.scalar);
    body.append(e.value);
    d[term_kind_str(e.kind)] = body;
  } else if (e.kind == TermExpr::Kind::Not) {
    d[term_kind_str(e.kind)] = e.operands.empty() ? py::object(py::none()) : to_py(e.operands[0]);
  } else {
    py::list body;
    for (auto const & op : e.operands) body.append(to_py(op));
    d[term_kind_str(e.kind)] = body;
  }
  return d;
}

template <>
void from_py(pybind11::object const & obj, TermExpr & out) {
  namespace py = pybind11;
  py::dict d = obj.cast<py::dict>();
  if (py::len(d) != 1)
    throw autocog::SchemaError("autocog::data: TermExpr must be a single-key dict", "term");
  auto it = d.begin();
  std::string key = it->first.cast<std::string>();
  if (!term_kind_from_str(key, out.kind))
    throw autocog::SchemaError("autocog::data: unknown TermExpr operator '" + key + "'", key);
  py::object val = py::reinterpret_borrow<py::object>(it->second);
  out.operands.clear();
  out.scalar.clear();
  out.value = 0.0f;
  if (term_is_comparison(out.kind)) {
    py::sequence s = val.cast<py::sequence>();
    out.scalar = s[0].cast<std::string>();
    out.value  = s[1].cast<float>();
  } else if (out.kind == TermExpr::Kind::Not) {
    out.operands.emplace_back();
    from_py(val, out.operands.back());
  } else {
    for (auto const & sub : val.cast<py::sequence>()) {
      out.operands.emplace_back();
      from_py(py::reinterpret_borrow<py::object>(sub), out.operands.back());
    }
    if (out.operands.empty())
      throw autocog::SchemaError("autocog::data: empty TermExpr combinator '" + key + "'", key);
  }
}

}
