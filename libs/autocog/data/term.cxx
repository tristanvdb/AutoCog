#include "autocog/data/term.hxx"

#include "autocog/data/utility.hxx"

#include <stdexcept>

namespace autocog::data {

std::string TermExpr::hash() const {
  ContentHasher h;
  h.put(static_cast<unsigned>(kind));
  h.put(scalar);
  h.put(value);
  h.put(static_cast<unsigned>(operands.size()));
  for (auto const & op : operands) h.put(op.hash());
  return h.hash();
}

namespace {

char const * kind_word(TermExpr::Kind k) {
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

bool word_kind(std::string const & w, TermExpr::Kind & out) {
  if (w == "all") { out = TermExpr::Kind::All; return true; }
  if (w == "any") { out = TermExpr::Kind::Any; return true; }
  if (w == "not") { out = TermExpr::Kind::Not; return true; }
  if (w == "ge")  { out = TermExpr::Kind::Ge;  return true; }
  if (w == "gt")  { out = TermExpr::Kind::Gt;  return true; }
  if (w == "le")  { out = TermExpr::Kind::Le;  return true; }
  if (w == "lt")  { out = TermExpr::Kind::Lt;  return true; }
  return false;
}

struct CompactParser {
  std::string const & s;
  size_t i = 0;
  explicit CompactParser(std::string const & s_) : s(s_) {}

  void ws() { while (i < s.size() && s[i] == ' ') ++i; }
  [[noreturn]] void fail() { throw std::invalid_argument("malformed TermExpr: " + s); }
  void expect(char c) { ws(); if (i >= s.size() || s[i] != c) fail(); ++i; }
  std::string word() {
    ws();
    size_t b = i;
    while (i < s.size() && s[i] != ' ' && s[i] != '(' && s[i] != ')') ++i;
    if (b == i) fail();
    return s.substr(b, i - b);
  }

  TermExpr expr() {
    expect('(');
    TermExpr e;
    if (!word_kind(word(), e.kind)) fail();
    if (e.kind == TermExpr::Kind::All || e.kind == TermExpr::Kind::Any
        || e.kind == TermExpr::Kind::Not) {
      ws();
      while (i < s.size() && s[i] == '(') { e.operands.push_back(expr()); ws(); }
    } else {
      e.scalar = word();
      e.value = std::stof(word());
    }
    expect(')');
    return e;
  }
};

}  // namespace

std::string TermExpr::to_compact() const {
  std::string out = "(";
  out += kind_word(kind);
  if (kind == Kind::All || kind == Kind::Any || kind == Kind::Not) {
    for (auto const & op : operands) { out += " "; out += op.to_compact(); }
  } else {
    out += " " + scalar + " " + std::to_string(value);
  }
  out += ")";
  return out;
}

TermExpr TermExpr::from_compact(std::string const & text) {
  CompactParser p(text);
  TermExpr e = p.expr();
  p.ws();
  if (p.i != text.size()) throw std::invalid_argument("malformed TermExpr: " + text);
  return e;
}

}
