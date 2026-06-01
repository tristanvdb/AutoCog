#ifndef AUTOCOG_RUNTIME_FTA_VOCAB_HXX
#define AUTOCOG_RUNTIME_FTA_VOCAB_HXX

#include <string>
#include <vector>
#include <memory>
#include <cstdint>

#include <nlohmann/json.hpp>

namespace autocog::runtime::fta {

// ---------------------------------------------------------------------------
// Vocab expressions  (shared TA-layer representation)
// ---------------------------------------------------------------------------
// A resolved vocabulary expression: the set of tokens a completion may emit.
// Reference-free by construction — all vocab identifier references are inlined
// during frontend lowering, so the tree holds only leaves (tokenize/regex) and
// set operations.
//
// This is the SINGLE representation shared across the pipeline: the compiler
// builds it (IR), STA/FTA transport it (serialized as a tree), and the backend
// (xfta) walks it to build token masks. It lives in runtime/ because runtime is
// the shared layer between frontends and backends; the compiler may depend on
// runtime, not the reverse.
//
// str() is used ONLY for hashing and for unparsing to STL* — never as the
// machine-consumed carry, which is always the structured tree (to/from_json).
struct VocabExpr {
  enum class Kind { Tokenize, Regex, Union, Intersect, Diff, Complement };

  Kind kind;
  std::vector<std::string> strings;                   // Tokenize: the strings; Regex: [pattern]
  std::vector<std::unique_ptr<VocabExpr>> operands;   // Union/Intersect/Diff: 2; Complement: 1

  // Unparse to an STL-parseable string: full operators and full parentheses, so
  // it round-trips through the parser and serves as the STL* form. This is the
  // string that gets hashed for the table key.
  std::string str() const {
    auto quote = [](std::string const & s) {
      std::string out = "\"";
      for (char c : s) {
        if (c == '"' || c == '\\') out += '\\';
        out += c;
      }
      out += "\"";
      return out;
    };
    switch (kind) {
      case Kind::Tokenize: {
        std::string out = "tokenize(";
        for (size_t i = 0; i < strings.size(); ++i) {
          if (i) out += ", ";
          out += quote(strings[i]);
        }
        return out + ")";
      }
      case Kind::Regex:
        return "regex(" + quote(strings.empty() ? std::string{} : strings[0]) + ")";
      case Kind::Union:      return "(" + operands[0]->str() + " | " + operands[1]->str() + ")";
      case Kind::Intersect:  return "(" + operands[0]->str() + " & " + operands[1]->str() + ")";
      case Kind::Diff:       return "(" + operands[0]->str() + " - " + operands[1]->str() + ")";
      case Kind::Complement: return "(!" + operands[0]->str() + ")";
    }
    return "";
  }

  // Canonical (simplified) form. A copy for now; the eventual set-algebra
  // simplification (idempotence, identities, De Morgan, leaf merging) goes here
  // and improves dedup, cache hits, and backend mask-construction cost.
  std::unique_ptr<VocabExpr> canonical() const {
    auto out = std::make_unique<VocabExpr>();
    out->kind = kind;
    out->strings = strings;
    for (auto const & op : operands) out->operands.push_back(op->canonical());
    return out;
  }
};

// String form of the kind, used as the JSON discriminant.
inline char const * vocab_kind_str(VocabExpr::Kind k) {
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

// Structured tree serialization — the machine-consumed carry through STA/FTA.
//   leaves: {"kind":"tokenize","strings":[...]}  /  {"kind":"regex","strings":["p"]}
//   ops:    {"kind":"union","operands":[<expr>,<expr>]}  etc.
inline nlohmann::json vocab_to_json(VocabExpr const & ve) {
  nlohmann::json j;
  j["kind"] = vocab_kind_str(ve.kind);
  if (ve.kind == VocabExpr::Kind::Tokenize || ve.kind == VocabExpr::Kind::Regex) {
    j["strings"] = ve.strings;
  } else {
    nlohmann::json ops = nlohmann::json::array();
    for (auto const & op : ve.operands) ops.push_back(vocab_to_json(*op));
    j["operands"] = ops;
  }
  return j;
}

inline std::unique_ptr<VocabExpr> vocab_from_json(nlohmann::json const & j) {
  auto ve = std::make_unique<VocabExpr>();
  std::string k = j.at("kind").get<std::string>();
  if      (k == "tokenize")   ve->kind = VocabExpr::Kind::Tokenize;
  else if (k == "regex")      ve->kind = VocabExpr::Kind::Regex;
  else if (k == "union")      ve->kind = VocabExpr::Kind::Union;
  else if (k == "intersect")  ve->kind = VocabExpr::Kind::Intersect;
  else if (k == "diff")       ve->kind = VocabExpr::Kind::Diff;
  else if (k == "complement") ve->kind = VocabExpr::Kind::Complement;
  if (ve->kind == VocabExpr::Kind::Tokenize || ve->kind == VocabExpr::Kind::Regex) {
    if (j.contains("strings"))
      for (auto const & s : j.at("strings")) ve->strings.push_back(s.get<std::string>());
  } else {
    if (j.contains("operands"))
      for (auto const & o : j.at("operands")) ve->operands.push_back(vocab_from_json(o));
  }
  return ve;
}

// Stable, dependency-free content hash of a vocab's canonical string (FNV-1a,
// 64-bit, lowercase hex). Used as the vocab table key and the reference a
// completion carries ("vocab_<hash>"). Deterministic across runs and languages.
inline std::string vocab_hash(VocabExpr const & ve) {
  std::string s = ve.canonical()->str();
  uint64_t h = 1469598103934665603ull;            // FNV offset basis
  for (unsigned char c : s) { h ^= c; h *= 1099511628211ull; } // FNV prime
  static char const * hex = "0123456789abcdef";
  std::string out(16, '0');
  for (int i = 15; i >= 0; --i) { out[i] = hex[h & 0xf]; h >>= 4; }
  return out;
}

}

#endif // AUTOCOG_RUNTIME_FTA_VOCAB_HXX
