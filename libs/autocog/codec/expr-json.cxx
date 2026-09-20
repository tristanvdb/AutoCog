#include "autocog/codec/json.hxx"

#include "autocog/data/namespaces.hxx"
#include "autocog/utilities/errors.hxx"

namespace autocog::codec {
using namespace autocog::data;

// An expression serializes as its VALUE when it is a literal, and as a
// single-key object otherwise:
//
//   5   2.5   true   null   "some text"     a literal, verbatim
//   {"ref": "__syntax__.prompt_zero_index"} a deferred reference
//   {"ge": [<expr>, <expr>]}                an operator (any op_word)
//   {"cond": [<expr>, <expr>, <expr>]}      a conditional
//   {"cat": [<expr>, ...]}                  a concatenation (f-string)
//
// The bare-literal case is why this is not a breaking change: every
// description, every search parameter and every argument in every artifact
// written so far is a literal, and it stays exactly the text or number it
// already was. Only an expression that actually defers grows a node.

namespace {

template <class... Ts> struct overloaded : Ts... { using Ts::operator()...; };
template <class... Ts> overloaded(Ts...) -> overloaded<Ts...>;

nlohmann::json value_to_json(autocog::types::Value const & v) {
  return std::visit(overloaded{
    [](int x) -> nlohmann::json { return x; },
    [](float x) -> nlohmann::json { return x; },
    [](bool x) -> nlohmann::json { return x; },
    [](std::string const & x) -> nlohmann::json { return x; },
    [](std::monostate) -> nlohmann::json { return nullptr; },
  }, v);
}

autocog::types::Value value_from_json(nlohmann::json const & j) {
  if (j.is_number_integer()) return j.get<int>();
  if (j.is_number_float())   return j.get<float>();
  if (j.is_boolean())        return j.get<bool>();
  if (j.is_string())         return j.get<std::string>();
  return std::monostate{};
}

/// Read an operand of an operator node. A bare string there is a literal
/// UNLESS it is one of the unqualified scalar names that termination
/// predicates used before the namespaces existed (`terminals`,
/// `model.n_ctx`, ...): those are references, and qualify() says so.
Expr operand_from_json(nlohmann::json const & j) {
  if (j.is_string()) {
    auto const name = j.get<std::string>();
    auto const qualified = namespaces::qualify(name);
    if (qualified != name) return Expr::reference(qualified);
  }
  Expr e;
  from_json(j, e);
  return e;
}

}  // namespace

template <>
nlohmann::json to_json(Expr const & e) {
  switch (e.kind) {
    case Expr::Kind::Lit:
      return value_to_json(e.value);
    case Expr::Kind::Ref:
      return nlohmann::json{{"ref", e.ref}};
    case Expr::Kind::Op:
    case Expr::Kind::Cond:
    case Expr::Kind::Cat: {
      nlohmann::json ops = nlohmann::json::array();
      for (auto const & o : e.operands) ops.push_back(to_json(o));
      char const * key = e.kind == Expr::Kind::Cond ? "cond"
                       : e.kind == Expr::Kind::Cat  ? "cat"
                                                    : op_word(e.op);
      return nlohmann::json{{key, ops}};
    }
  }
  return nullptr;
}

template <>
void from_json(nlohmann::json const & j, Expr & e) {
  autocog::codec::read_guarded("Expr", [&]{
    if (!j.is_object()) { e = Expr::lit(value_from_json(j)); return; }
    if (j.size() != 1)
      throw autocog::SchemaError("an expression object takes exactly one key "
                                 "(got " + std::to_string(j.size()) + ")", "expr");
    auto const it = j.begin();
    std::string const key = it.key();

    if (key == "ref") {
      e = Expr::reference(namespaces::qualify(it.value().get<std::string>()));
      return;
    }

    if (!it.value().is_array())
      throw autocog::SchemaError("expression operator '" + key
                                 + "' expects an array of operands", key);

    std::vector<Expr> operands;
    for (auto const & o : it.value()) operands.push_back(operand_from_json(o));

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
  });
}

}
