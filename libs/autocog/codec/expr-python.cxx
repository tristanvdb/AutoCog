#include "autocog/codec/python.hxx"

#include "autocog/data/namespaces.hxx"
#include "autocog/utilities/errors.hxx"

namespace py = pybind11;

namespace autocog::codec {
using namespace autocog::data;

// The Python mirror of the JSON form (see expr-json.cxx): a literal is the
// bare value, anything else is a one-key dict.

namespace {

template <class... Ts> struct overloaded : Ts... { using Ts::operator()...; };
template <class... Ts> overloaded(Ts...) -> overloaded<Ts...>;

py::object value_to_py(autocog::types::Value const & v) {
  return std::visit(overloaded{
    [](int x) -> py::object { return py::cast(x); },
    [](float x) -> py::object { return py::cast(x); },
    [](bool x) -> py::object { return py::cast(x); },
    [](std::string const & x) -> py::object { return py::cast(x); },
    [](std::monostate) -> py::object { return py::none(); },
  }, v);
}

autocog::types::Value value_from_py(py::handle h) {
  if (py::isinstance<py::bool_>(h))  return h.cast<bool>();   // bool before int
  if (py::isinstance<py::int_>(h))   return h.cast<int>();
  if (py::isinstance<py::float_>(h)) return h.cast<float>();
  if (py::isinstance<py::str>(h))    return h.cast<std::string>();
  return std::monostate{};
}

Expr operand_from_py(py::handle h) {
  if (py::isinstance<py::str>(h)) {
    auto const name = h.cast<std::string>();
    auto const qualified = namespaces::qualify(name);
    if (qualified != name) return Expr::reference(qualified);
  }
  Expr e;
  from_py(py::reinterpret_borrow<py::object>(h), e);
  return e;
}

}  // namespace

template <>
py::object to_py(Expr const & e) {
  switch (e.kind) {
    case Expr::Kind::Lit:
      return value_to_py(e.value);
    case Expr::Kind::Ref: {
      py::dict d;
      d["ref"] = e.ref;
      return d;
    }
    case Expr::Kind::Op:
    case Expr::Kind::Cond:
    case Expr::Kind::Cat: {
      py::list ops;
      for (auto const & o : e.operands) ops.append(to_py(o));
      char const * key = e.kind == Expr::Kind::Cond ? "cond"
                       : e.kind == Expr::Kind::Cat  ? "cat"
                                                    : op_word(e.op);
      py::dict d;
      d[py::str(key)] = ops;
      return d;
    }
  }
  return py::none();
}

template <>
void from_py(py::object const & obj, Expr & e) {
  if (!py::isinstance<py::dict>(obj)) { e = Expr::lit(value_from_py(obj)); return; }
  py::dict d = obj.cast<py::dict>();
  if (d.size() != 1)
    throw autocog::SchemaError("an expression dict takes exactly one key "
                               "(got " + std::to_string(d.size()) + ")", "expr");
  auto const item = *d.begin();
  std::string const key = item.first.cast<std::string>();

  if (key == "ref") {
    e = Expr::reference(namespaces::qualify(item.second.cast<std::string>()));
    return;
  }

  if (!py::isinstance<py::list>(item.second) && !py::isinstance<py::tuple>(item.second))
    throw autocog::SchemaError("expression operator '" + key
                               + "' expects a list of operands", key);

  std::vector<Expr> operands;
  for (auto const & o : item.second) operands.push_back(operand_from_py(o));

  if (key == "cond") {
    if (operands.size() != 3)
      throw autocog::SchemaError("'cond' takes exactly three operands "
                                 "(condition, then, else)", key);
    e = Expr::conditional(operands[0], operands[1], operands[2]);
    return;
  }
  if (key == "cat") { e = Expr::concat(std::move(operands)); return; }

  OpKind op;
  if (!op_from_word(key, op))
    throw autocog::SchemaError("'" + key + "' is not an expression operator", key);
  if (operands.empty())
    throw autocog::SchemaError("operator '" + key + "' has no operands", key);
  if (op_is_unary(op) && operands.size() != 1)
    throw autocog::SchemaError("operator '" + key + "' takes one operand", key);
  e = Expr::variadic(op, std::move(operands));
}

}
