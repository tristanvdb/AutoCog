#include "autocog/data/expr.hxx"
#include "autocog/data/namespaces.hxx"
#include "autocog/data/utility.hxx"
#include "autocog/data/expr-ops.txx"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <stdexcept>

namespace autocog::data {

// ---------------------------------------------------------------------------
// Operators
// ---------------------------------------------------------------------------

std::string opKindToString(OpKind op) {
  switch (op) {
    case OpKind::NOP: return "NOP";
    case OpKind::Not: return "Not";
    case OpKind::Neg: return "Neg";
    case OpKind::Add: return "Add";
    case OpKind::Sub: return "Sub";
    case OpKind::Mul: return "Mul";
    case OpKind::Div: return "Div";
    case OpKind::Mod: return "Mod";
    case OpKind::And: return "And";
    case OpKind::Or:  return "Or";
    case OpKind::BAnd: return "BAnd";
    case OpKind::BOr:  return "BOr";
    case OpKind::Lt:  return "Lt";
    case OpKind::Gt:  return "Gt";
    case OpKind::Lte: return "Lte";
    case OpKind::Gte: return "Gte";
    case OpKind::Eq:  return "Eq";
    case OpKind::Neq: return "Neq";
    default: return "Unknown(" + std::to_string(static_cast<int>(op)) + ")";
  }
}

char const * op_word(OpKind op) {
  switch (op) {
    case OpKind::NOP: return "nop";
    case OpKind::Not: return "not";
    case OpKind::Neg: return "neg";
    case OpKind::Add: return "add";
    case OpKind::Sub: return "sub";
    case OpKind::Mul: return "mul";
    case OpKind::Div: return "div";
    case OpKind::Mod: return "mod";
    case OpKind::And: return "and";
    case OpKind::Or:  return "or";
    case OpKind::BAnd: return "band";
    case OpKind::BOr:  return "bor";
    case OpKind::Lt:  return "lt";
    case OpKind::Gt:  return "gt";
    case OpKind::Lte: return "le";
    case OpKind::Gte: return "ge";
    case OpKind::Eq:  return "eq";
    case OpKind::Neq: return "ne";
  }
  return "nop";
}

bool op_from_word(std::string const & w, OpKind & out) {
  static std::pair<char const *, OpKind> const table[] = {
    {"not", OpKind::Not}, {"neg", OpKind::Neg},
    {"add", OpKind::Add}, {"sub", OpKind::Sub}, {"mul", OpKind::Mul},
    {"div", OpKind::Div}, {"mod", OpKind::Mod},
    {"and", OpKind::And}, {"or", OpKind::Or},
    {"band", OpKind::BAnd}, {"bor", OpKind::BOr},
    {"lt", OpKind::Lt}, {"gt", OpKind::Gt},
    {"le", OpKind::Lte}, {"ge", OpKind::Gte},
    {"eq", OpKind::Eq}, {"ne", OpKind::Neq},
    // The predicate spelling that the termination compact form used before the
    // expression language was shared. `all`/`any` are the variadic and/or.
    {"all", OpKind::And}, {"any", OpKind::Or},
  };
  for (auto const & [word, kind] : table)
    if (w == word) { out = kind; return true; }
  return false;
}

char const * op_symbol(OpKind op) {
  switch (op) {
    case OpKind::NOP: return "";
    case OpKind::Not: return "!";
    case OpKind::Neg: return "-";
    case OpKind::Add: return "+";
    case OpKind::Sub: return "-";
    case OpKind::Mul: return "*";
    case OpKind::Div: return "/";
    case OpKind::Mod: return "%";
    case OpKind::And: return "&&";
    case OpKind::Or:  return "||";
    case OpKind::BAnd: return "&";
    case OpKind::BOr:  return "|";
    case OpKind::Lt:  return "<";
    case OpKind::Gt:  return ">";
    case OpKind::Lte: return "<=";
    case OpKind::Gte: return ">=";
    case OpKind::Eq:  return "==";
    case OpKind::Neq: return "!=";
  }
  return "";
}

bool op_is_unary(OpKind op) { return op == OpKind::Not || op == OpKind::Neg; }

bool op_is_variadic(OpKind op) {
  return op == OpKind::And || op == OpKind::Or
      || op == OpKind::BAnd || op == OpKind::BOr;
}

Value apply_unary(OpKind op, Value const & operand,
                  std::optional<autocog::location::SourceRange> const & loc) {
  switch (op) {
    case OpKind::Neg:
      return std::visit([&loc](auto const & v) -> Value {
        using V = std::decay_t<decltype(v)>;
        if constexpr (std::is_same_v<V, int> || std::is_same_v<V, float>) return -v;
        else throw CompileError("Cannot negate non-numeric value", loc);
      }, operand);
    case OpKind::Not:
      return std::visit([&loc](auto const & v) -> Value {
        using V = std::decay_t<decltype(v)>;
        if constexpr (std::is_same_v<V, bool>) return !v;
        else throw CompileError("Cannot apply 'not' to non-boolean value", loc);
      }, operand);
    default:
      throw autocog::utilities::InternalError(
          "Invalid unary operator kind: " + opKindToString(op));
  }
}

Value apply_binary(OpKind op, Value const & lhs, Value const & rhs,
                   std::optional<autocog::location::SourceRange> const & loc) {
  switch (op) {
    case OpKind::Add: return ops::evaluateArithmetic<OpKind::Add>(lhs, rhs, loc);
    case OpKind::Sub: return ops::evaluateArithmetic<OpKind::Sub>(lhs, rhs, loc);
    case OpKind::Mul: return ops::evaluateArithmetic<OpKind::Mul>(lhs, rhs, loc);
    case OpKind::Div: return ops::evaluateArithmetic<OpKind::Div>(lhs, rhs, loc);
    case OpKind::Mod: return ops::evaluateArithmetic<OpKind::Mod>(lhs, rhs, loc);

    case OpKind::Eq:  return ops::evaluateComparison<OpKind::Eq>(lhs, rhs, loc);
    case OpKind::Neq: return ops::evaluateComparison<OpKind::Neq>(lhs, rhs, loc);
    case OpKind::Lt:  return ops::evaluateComparison<OpKind::Lt>(lhs, rhs, loc);
    case OpKind::Lte: return ops::evaluateComparison<OpKind::Lte>(lhs, rhs, loc);
    case OpKind::Gt:  return ops::evaluateComparison<OpKind::Gt>(lhs, rhs, loc);
    case OpKind::Gte: return ops::evaluateComparison<OpKind::Gte>(lhs, rhs, loc);

    case OpKind::And: return ops::evaluateLogical<OpKind::And>(lhs, rhs, loc);
    case OpKind::Or:  return ops::evaluateLogical<OpKind::Or>(lhs, rhs, loc);

    case OpKind::BOr:  return ops::evaluateBitwise<OpKind::BOr>(lhs, rhs, loc);
    case OpKind::BAnd: return ops::evaluateBitwise<OpKind::BAnd>(lhs, rhs, loc);

    default:
      throw autocog::utilities::InternalError("Invalid binary operator kind");
  }
}

// ---------------------------------------------------------------------------
// Stages
// ---------------------------------------------------------------------------

char const * stage_word(Stage s) {
  switch (s) {
    case Stage::Compile:     return "compile";
    case Stage::Instantiate: return "instantiate";
    case Stage::Execute:     return "execute";
  }
  return "compile";
}

Stage ref_stage(std::string const & ref) { return namespaces::stage_of(ref); }

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------

Expr Expr::lit(Value v)       { Expr e; e.kind = Kind::Lit; e.value = std::move(v); return e; }
Expr Expr::lit(int v)         { return lit(Value{v}); }
Expr Expr::lit(float v)       { return lit(Value{v}); }
Expr Expr::lit(bool v)        { return lit(Value{v}); }
Expr Expr::lit(std::string v) { return lit(Value{std::move(v)}); }

Expr Expr::reference(std::string r) {
  Expr e;
  e.kind = Kind::Ref;
  e.ref = std::move(r);
  return e;
}

Expr Expr::unary(OpKind op, Expr operand) {
  Expr e;
  e.kind = Kind::Op;
  e.op = op;
  e.operands.push_back(std::move(operand));
  return e;
}

Expr Expr::binary(OpKind op, Expr lhs, Expr rhs) {
  Expr e;
  e.kind = Kind::Op;
  e.op = op;
  e.operands.push_back(std::move(lhs));
  e.operands.push_back(std::move(rhs));
  return e;
}

Expr Expr::variadic(OpKind op, std::vector<Expr> operands) {
  Expr e;
  e.kind = Kind::Op;
  e.op = op;
  e.operands = std::move(operands);
  return e;
}

Expr Expr::conditional(Expr cond, Expr e_true, Expr e_false) {
  Expr e;
  e.kind = Kind::Cond;
  e.operands.push_back(std::move(cond));
  e.operands.push_back(std::move(e_true));
  e.operands.push_back(std::move(e_false));
  return e;
}

Expr Expr::concat(std::vector<Expr> parts) {
  Expr e;
  e.kind = Kind::Cat;
  e.operands = std::move(parts);
  return e;
}

std::optional<Value> Expr::as_value() const {
  if (kind != Kind::Lit) return std::nullopt;
  return value;
}

void Expr::collect_refs(std::vector<std::string> & out) const {
  if (kind == Kind::Ref) out.push_back(ref);
  for (auto const & o : operands) o.collect_refs(out);
}

Stage Expr::stage() const {
  Stage s = Stage::Compile;
  std::vector<std::string> refs;
  collect_refs(refs);
  for (auto const & r : refs) {
    Stage rs = ref_stage(r);
    if (static_cast<unsigned>(rs) > static_cast<unsigned>(s)) s = rs;
  }
  return s;
}

bool Expr::operator==(Expr const & other) const {
  if (kind != other.kind || op != other.op || ref != other.ref) return false;
  if (kind == Kind::Lit && value != other.value) return false;
  if (operands.size() != other.operands.size()) return false;
  for (size_t i = 0; i < operands.size(); ++i)
    if (!(operands[i] == other.operands[i])) return false;
  return true;
}

// ---------------------------------------------------------------------------
// Identity
// ---------------------------------------------------------------------------

std::string Expr::hash() const {
  ContentHasher h;
  h.put(static_cast<unsigned>(kind));
  h.put(static_cast<unsigned>(op));
  h.put(ref);
  // The value's TYPE is hashed with it: 1 and 1.0 and "1" are different
  // expressions even where an operator would coerce them.
  h.put(static_cast<unsigned>(value.index()));
  std::visit([&h](auto const & v) {
    using V = std::decay_t<decltype(v)>;
    if constexpr (std::is_same_v<V, std::monostate>) h.put(std::string{});
    else h.put(v);
  }, value);
  h.put(static_cast<unsigned>(operands.size()));
  for (auto const & o : operands) h.put(o.hash());
  return h.hash();
}

// ---------------------------------------------------------------------------
// Text forms
// ---------------------------------------------------------------------------

namespace {

std::string quote(std::string const & s) {
  std::string out = "\"";
  for (char c : s) {
    if (c == '"' || c == '\\') out += '\\';
    out += c;
  }
  return out + "\"";
}

/// A float as it READS: no trailing zeros, no trailing point. What an
/// interpolated value looks like when it lands in a prompt.
std::string float_text(float f) {
  std::string s = std::to_string(f);
  if (s.find('.') != std::string::npos) {
    s.erase(s.find_last_not_of('0') + 1, std::string::npos);
    if (!s.empty() && s.back() == '.') s.pop_back();
  }
  return s;
}

/// A float as SOURCE: always with a decimal point, so that it reads back as a
/// float and not as an int. The two are different expressions -- the type is
/// part of the identity -- and `5` would come back as the wrong one.
std::string float_literal(float f) {
  std::string s = float_text(f);
  if (s.find('.') == std::string::npos && s.find('e') == std::string::npos
      && s.find("inf") == std::string::npos && s.find("nan") == std::string::npos)
    s += ".0";
  return s;
}

}  // namespace

std::string stringify(Value const & v) {
  return std::visit([](auto const & x) -> std::string {
    using V = std::decay_t<decltype(x)>;
    if constexpr (std::is_same_v<V, int>) return std::to_string(x);
    else if constexpr (std::is_same_v<V, float>) return float_text(x);
    else if constexpr (std::is_same_v<V, bool>) return x ? "true" : "false";
    else if constexpr (std::is_same_v<V, std::string>) return x;
    else return std::string{};   // monostate: null renders as nothing
  }, v);
}

std::string Expr::str() const {
  switch (kind) {
    case Kind::Lit:
      return std::visit([](auto const & v) -> std::string {
        using V = std::decay_t<decltype(v)>;
        if constexpr (std::is_same_v<V, std::string>) return quote(v);
        else if constexpr (std::is_same_v<V, std::monostate>) return "null";
        else if constexpr (std::is_same_v<V, bool>) return v ? "true" : "false";
        else if constexpr (std::is_same_v<V, float>) return float_literal(v);
        else return std::to_string(v);
      }, value);
    case Kind::Ref:
      return ref;
    case Kind::Op:
      if (op_is_unary(op)) return "(" + std::string(op_symbol(op)) + operands[0].str() + ")";
      else {
        std::string out = "(";
        for (size_t i = 0; i < operands.size(); ++i) {
          if (i) out += std::string(" ") + op_symbol(op) + " ";
          out += operands[i].str();
        }
        return out + ")";
      }
    case Kind::Cond:
      return "(" + operands[0].str() + " ? " + operands[1].str()
           + " : " + operands[2].str() + ")";
    case Kind::Cat: {
      // An f-string unparses as one: the literal parts are the text, the rest
      // are interpolations. Round-trips through the STL string parser.
      std::string out = "f\"";
      for (auto const & o : operands) {
        if (o.kind == Kind::Lit && std::holds_alternative<std::string>(o.value)) {
          for (char c : std::get<std::string>(o.value)) {
            if (c == '{' || c == '}') out += c;      // doubled below
            if (c == '"' || c == '\\') out += '\\';
            out += c;
          }
        } else {
          out += "{" + o.str() + "}";
        }
      }
      return out + "\"";
    }
  }
  return "";
}

std::string Expr::to_compact() const {
  switch (kind) {
    case Kind::Lit:
      return std::visit([](auto const & v) -> std::string {
        using V = std::decay_t<decltype(v)>;
        if constexpr (std::is_same_v<V, std::string>) return quote(v);
        else if constexpr (std::is_same_v<V, std::monostate>) return "null";
        else if constexpr (std::is_same_v<V, bool>) return v ? "true" : "false";
        else if constexpr (std::is_same_v<V, float>) return float_literal(v);
        else return std::to_string(v);
      }, value);
    case Kind::Ref:
      return "@" + ref;
    case Kind::Op: {
      std::string out = std::string("(") + op_word(op);
      for (auto const & o : operands) out += " " + o.to_compact();
      return out + ")";
    }
    case Kind::Cond: {
      std::string out = "(cond";
      for (auto const & o : operands) out += " " + o.to_compact();
      return out + ")";
    }
    case Kind::Cat: {
      std::string out = "(cat";
      for (auto const & o : operands) out += " " + o.to_compact();
      return out + ")";
    }
  }
  return "";
}

namespace {

/// Reader for the compact s-expression form. It also accepts the shape the
/// termination predicate used before expressions were shared --
/// `(ge terminals 5)`, where the left operand is a bare scalar name and the
/// right is a number or `@ref` -- so artifacts written by earlier versions
/// still load. Bare names are resolved to their namespace by the registry.
struct CompactParser {
  std::string const & s;
  size_t i = 0;
  explicit CompactParser(std::string const & s_) : s(s_) {}

  void ws() { while (i < s.size() && s[i] == ' ') ++i; }
  [[noreturn]] void fail() const { throw std::invalid_argument("malformed expression: " + s); }
  void expect(char c) { ws(); if (i >= s.size() || s[i] != c) fail(); ++i; }

  std::string word() {
    ws();
    size_t b = i;
    while (i < s.size() && s[i] != ' ' && s[i] != '(' && s[i] != ')') ++i;
    if (b == i) fail();
    return s.substr(b, i - b);
  }

  std::string quoted() {
    expect('"');
    std::string out;
    while (i < s.size() && s[i] != '"') {
      if (s[i] == '\\' && i + 1 < s.size()) ++i;
      out += s[i++];
    }
    expect('"');
    return out;
  }

  /// A leaf: quoted string, @ref, keyword, or number. A bare word that is not a
  /// keyword or a number is a legacy scalar name.
  Expr atom() {
    ws();
    if (i >= s.size()) fail();
    if (s[i] == '"') return Expr::lit(quoted());
    std::string w = word();
    if (w.empty()) fail();
    if (w[0] == '@') return Expr::reference(w.substr(1));
    if (w == "true") return Expr::lit(true);
    if (w == "false") return Expr::lit(false);
    if (w == "null") return Expr::lit(Value{std::monostate{}});
    char * end = nullptr;
    long const as_int = std::strtol(w.c_str(), &end, 10);
    if (end && *end == '\0') return Expr::lit(static_cast<int>(as_int));
    end = nullptr;
    float const as_float = std::strtof(w.c_str(), &end);
    if (end && *end == '\0') return Expr::lit(as_float);
    return Expr::reference(namespaces::qualify(w));
  }

  Expr expr() {
    ws();
    if (i < s.size() && s[i] == '(') {
      ++i;
      std::string head = word();
      Expr e;
      if (head == "cond") e.kind = Expr::Kind::Cond;
      else if (head == "cat") e.kind = Expr::Kind::Cat;
      else {
        e.kind = Expr::Kind::Op;
        if (!op_from_word(head, e.op)) fail();
      }
      ws();
      while (i < s.size() && s[i] != ')') { e.operands.push_back(expr()); ws(); }
      expect(')');
      if (e.kind == Expr::Kind::Cond && e.operands.size() != 3) fail();
      if (e.kind == Expr::Kind::Op && e.operands.empty()) fail();
      return e;
    }
    return atom();
  }
};

}  // namespace

Expr Expr::from_compact(std::string const & text) {
  CompactParser p(text);
  Expr e = p.expr();
  p.ws();
  if (p.i != text.size()) throw std::invalid_argument("malformed expression: " + text);
  return e;
}

// ---------------------------------------------------------------------------
// Evaluation
// ---------------------------------------------------------------------------

Resolver const & no_refs() {
  static Resolver const none =
      [](std::string const &) -> std::optional<Value> { return std::nullopt; };
  return none;
}

Expr residual(Expr const & e, Resolver const & resolve,
              std::optional<autocog::location::SourceRange> const & loc) {
  switch (e.kind) {
    case Expr::Kind::Lit:
      return e;

    case Expr::Kind::Ref: {
      auto bound = resolve(e.ref);
      if (!bound) return e;                     // not this stage's to bind
      return Expr::lit(*bound);
    }

    case Expr::Kind::Op: {
      std::vector<Expr> parts;
      parts.reserve(e.operands.size());
      bool all_lit = true;
      for (auto const & o : e.operands) {
        parts.push_back(residual(o, resolve, loc));
        all_lit = all_lit && parts.back().is_literal();
      }
      if (!all_lit) return Expr::variadic(e.op, std::move(parts));
      if (op_is_unary(e.op)) return Expr::lit(apply_unary(e.op, parts[0].value, loc));
      Value acc = parts[0].value;
      for (size_t i = 1; i < parts.size(); ++i)
        acc = apply_binary(e.op, acc, parts[i].value, loc);
      return Expr::lit(std::move(acc));
    }

    case Expr::Kind::Cond: {
      // The branch not taken is DROPPED, errors and all: `n > 0 ? 1/n : 0` must
      // fold to 0 at n = 0, not raise. That is also what makes a knob-switched
      // annotation collapse to plain text for every syntax that fixes the knob.
      Expr cond = residual(e.operands[0], resolve, loc);
      if (cond.is_literal()) {
        bool const taken = std::visit([&loc](auto const & v) -> bool {
          using V = std::decay_t<decltype(v)>;
          if constexpr (std::is_same_v<V, bool>) return v;
          else throw CompileError("Conditional expression requires boolean condition", loc);
        }, cond.value);
        return residual(e.operands[taken ? 1 : 2], resolve, loc);
      }
      return Expr::conditional(std::move(cond),
                               residual(e.operands[1], resolve, loc),
                               residual(e.operands[2], resolve, loc));
    }

    case Expr::Kind::Cat: {
      // Stringify what is known and merge neighbours, so a fully-resolved
      // f-string comes out as one literal -- the shape every existing program
      // produces, and the reason none of them pay for this.
      std::vector<Expr> parts;
      for (auto const & o : e.operands) {
        Expr r = residual(o, resolve, loc);
        if (r.is_literal()) {
          std::string text = stringify(r.value);
          if (text.empty()) continue;
          if (!parts.empty() && parts.back().is_literal()
              && std::holds_alternative<std::string>(parts.back().value)) {
            std::get<std::string>(parts.back().value) += text;
            continue;
          }
          parts.push_back(Expr::lit(std::move(text)));
        } else {
          parts.push_back(std::move(r));
        }
      }
      if (parts.empty()) return Expr::lit(std::string{});
      if (parts.size() == 1 && parts[0].is_literal()) return std::move(parts[0]);
      return Expr::concat(std::move(parts));
    }
  }
  return e;
}

Value evaluate(Expr const & e, Resolver const & resolve,
               std::optional<autocog::location::SourceRange> const & loc) {
  Expr const r = residual(e, resolve, loc);
  if (auto v = r.as_value()) return *v;
  std::vector<std::string> refs;
  r.collect_refs(refs);
  std::string const what = refs.empty() ? std::string("an expression") : ("`" + refs[0] + "`");
  throw CompileError("Cannot evaluate " + what + " here: it is bound at "
                     + stage_word(r.stage()) + " time.", loc);
}

std::string render(Expr const & e, Resolver const & resolve,
                   std::optional<autocog::location::SourceRange> const & loc) {
  return stringify(evaluate(e, resolve, loc));
}

}
