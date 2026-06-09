#include "autocog/codec/json.hxx"

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
nlohmann::json to_json(VocabExpr const & ve) {
  nlohmann::json j;
  j["kind"] = kind_str(ve.kind);
  if (is_leaf(ve.kind)) {
    j["strings"] = ve.strings;
  } else {
    nlohmann::json ops = nlohmann::json::array();
    for (auto const & op : ve.operands) ops.push_back(to_json(op));
    j["operands"] = ops;
  }
  return j;
}

template <>
void from_json(nlohmann::json const & dom, VocabExpr & ve) {
  autocog::codec::read_guarded("VocabExpr", [&]{
  ve.kind = kind_from_str(dom.at("kind").get<std::string>());
  if (is_leaf(ve.kind)) {
    if (dom.contains("strings")) ve.strings = dom.at("strings").get<std::vector<std::string>>();
  } else {
    if (dom.contains("operands"))
      for (auto const & o : dom.at("operands")) {
        ve.operands.emplace_back();
        from_json(o, ve.operands.back());
      }
  }
  });
}

}
