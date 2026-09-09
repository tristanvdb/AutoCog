#include "autocog/codec/json.hxx"

namespace autocog::codec {
using namespace autocog::data;

// A TermExpr serializes as a single-key object:
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
nlohmann::json to_json(TermExpr const & e) {
  nlohmann::json body;
  if (term_is_comparison(e.kind)) {
    body = nlohmann::json::array({e.scalar, e.value});
  } else if (e.kind == TermExpr::Kind::Not) {
    body = e.operands.empty() ? nlohmann::json() : to_json(e.operands[0]);
  } else {
    body = nlohmann::json::array();
    for (auto const & op : e.operands) body.push_back(to_json(op));
  }
  return nlohmann::json{{term_kind_str(e.kind), std::move(body)}};
}

template <>
void from_json(nlohmann::json const & dom, TermExpr & out) {
  autocog::codec::read_guarded("TermExpr", [&]{
  if (!dom.is_object() || dom.size() != 1)
    throw autocog::SchemaError("autocog::data: TermExpr must be a single-key object", "term");
  auto const it = dom.begin();
  if (!term_kind_from_str(it.key(), out.kind))
    throw autocog::SchemaError("autocog::data: unknown TermExpr operator '" + it.key() + "'", it.key());
  out.operands.clear();
  out.scalar.clear();
  out.value = 0.0f;
  if (term_is_comparison(out.kind)) {
    out.scalar = it.value().at(0).get<std::string>();
    out.value  = it.value().at(1).get<float>();
  } else if (out.kind == TermExpr::Kind::Not) {
    out.operands.emplace_back();
    from_json(it.value(), out.operands.back());
  } else {
    for (auto const & sub : it.value()) {
      out.operands.emplace_back();
      from_json(sub, out.operands.back());
    }
    if (out.operands.empty())
      throw autocog::SchemaError("autocog::data: empty TermExpr combinator '" + it.key() + "'", it.key());
  }
  });
}

}
