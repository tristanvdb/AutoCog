// Unit test for the shared expression language: partial evaluation, staging,
// the text forms, and identity. Returns non-zero if any check fails.
//
// The behaviour under test is the one the rest of the pipeline relies on: an
// expression narrows as it travels, and what comes out the far end is either a
// literal or a tree whose only remaining references belong to a later stage.

#include "autocog/data/expr.hxx"
#include "autocog/data/namespaces.hxx"

#include <iostream>
#include <map>
#include <string>

namespace {

int failures = 0;
void check(bool ok, std::string const & what) {
  if (ok) std::cout << "ok   : " << what << "\n";
  else  { std::cerr << "FAIL : " << what << "\n"; ++failures; }
}

using autocog::data::Expr;
using autocog::data::OpKind;
using autocog::data::Stage;
using autocog::data::Value;

/// A resolver over a fixed table: anything not in it stays deferred.
autocog::data::Resolver from(std::map<std::string, Value> table) {
  return [table = std::move(table)](std::string const & ref) -> std::optional<Value> {
    auto it = table.find(ref);
    if (it == table.end()) return std::nullopt;
    return it->second;
  };
}

bool threw_compile_error(std::function<void()> const & fn) {
  try { fn(); } catch (autocog::CompileError const &) { return true; }
  return false;
}

}

int main() {
  using autocog::data::residual;
  using autocog::data::evaluate;
  using autocog::data::render;
  using autocog::data::no_refs;

  auto const zero_index = Expr::reference("__syntax__.prompt_zero_index");

  // --- folding ------------------------------------------------------------
  {
    auto e = Expr::binary(OpKind::Add, Expr::lit(2), Expr::lit(3));
    auto r = residual(e, no_refs());
    check(r.is_literal() && std::get<int>(*r.as_value()) == 5,
          "a reference-free expression folds to a literal");
  }
  {
    auto e = Expr::unary(OpKind::Not, zero_index);
    auto r = residual(e, no_refs());
    check(!r.is_literal(), "an unbound reference keeps the expression alive");
    check(r.stage() == Stage::Instantiate, "its stage is the reference's stage");
    auto bound = residual(e, from({{"__syntax__.prompt_zero_index", Value{true}}}));
    check(bound.is_literal() && std::get<bool>(*bound.as_value()) == false,
          "binding the reference folds the whole expression");
  }

  // --- conditionals: the untaken branch is dropped, errors and all ---------
  {
    auto e = Expr::conditional(zero_index, Expr::lit(std::string(", counted from 0")),
                                           Expr::lit(std::string(", counted from 1")));
    auto zero = residual(e, from({{"__syntax__.prompt_zero_index", Value{true}}}));
    auto one  = residual(e, from({{"__syntax__.prompt_zero_index", Value{false}}}));
    check(zero.is_literal() && std::get<std::string>(*zero.as_value()) == ", counted from 0",
          "a known condition selects its branch");
    check(one.is_literal() && std::get<std::string>(*one.as_value()) == ", counted from 1",
          "and the other one when it is false");
    check(!residual(e, no_refs()).is_literal(),
          "an unknown condition keeps both branches");
  }
  {
    // 1/0 in the untaken branch must not raise: the branch is never evaluated.
    auto boom = Expr::binary(OpKind::Div, Expr::lit(1), Expr::lit(0));
    auto e = Expr::conditional(Expr::lit(false), boom, Expr::lit(7));
    auto r = residual(e, no_refs());
    check(r.is_literal() && std::get<int>(*r.as_value()) == 7,
          "the untaken branch is dropped without being evaluated");
  }

  // --- concatenation (f-strings) ------------------------------------------
  {
    auto e = Expr::concat({Expr::lit(std::string("the choice")),
                           Expr::lit(std::string(" at ")),
                           Expr::lit(3)});
    auto r = residual(e, no_refs());
    check(r.is_literal() && std::get<std::string>(*r.as_value()) == "the choice at 3",
          "a fully-known f-string collapses to one literal");
  }
  {
    auto e = Expr::concat({Expr::lit(std::string("index")), zero_index});
    auto r = residual(e, no_refs());
    check(r.kind == Expr::Kind::Cat && r.operands.size() == 2,
          "a deferred part keeps the concatenation");
    auto done = residual(r, from({{"__syntax__.prompt_zero_index", Value{false}}}));
    check(done.is_literal() && std::get<std::string>(*done.as_value()) == "indexfalse",
          "and it finishes when the reference binds -- residual() is re-entrant");
  }

  // --- evaluate / render ---------------------------------------------------
  {
    check(render(Expr::lit(2.5f), no_refs()) == "2.5", "floats render without trailing zeros");
    check(render(Expr::lit(true), no_refs()) == "true", "booleans render as words");
    check(render(Expr::lit(Value{std::monostate{}}), no_refs()).empty(),
          "null renders as nothing");
    check(threw_compile_error([&] { evaluate(zero_index, no_refs()); }),
          "evaluate() refuses an expression that is still deferred");
  }

  // --- staging -------------------------------------------------------------
  {
    check(Expr::lit(1).stage() == Stage::Compile, "a literal is bound at compile time");
    check(Expr::reference("__model__.n_ctx").stage() == Stage::Execute,
          "a model property is bound at execution");
    auto mixed = Expr::binary(OpKind::Lt, Expr::reference("__status__.tree.tokens"),
                              Expr::reference("__model__.n_ctx"));
    check(mixed.stage() == Stage::Execute, "a tree takes the latest stage it contains");
    check(threw_compile_error([] { autocog::data::namespaces::check("__syntax__.nope"); }),
          "an unknown field of a known namespace is an error");
    check(threw_compile_error([] { autocog::data::namespaces::check("__nope__.x"); }),
          "an unknown namespace is an error");
    check(threw_compile_error([] { autocog::data::namespaces::check("__syntax__"); }),
          "a namespace is not a value");
    autocog::data::namespaces::check("__search__.enum.width");
    check(true, "a search parameter is a known reference");
  }

  // --- text forms ----------------------------------------------------------
  {
    auto e = Expr::conditional(zero_index, Expr::lit(0), Expr::lit(1));
    auto round = Expr::from_compact(e.to_compact());
    check(round == e, "the compact form round-trips");
    check(e.to_compact() == "(cond @__syntax__.prompt_zero_index 0 1)",
          "and it reads as an s-expression");
    auto cat = Expr::concat({Expr::lit(std::string("a")), zero_index});
    check(Expr::from_compact(cat.to_compact()) == cat, "including concatenations");
    auto cmp = Expr::binary(OpKind::Gte, Expr::reference("__status__.tree.terminals"), Expr::lit(5.0f));
    check(Expr::from_compact(cmp.to_compact()) == cmp, "and comparisons");
  }
  {
    // The shape termination predicates were written in before the expression
    // language was shared. Artifacts holding it must still load.
    auto legacy = Expr::from_compact("(any (ge terminals 5) (lt tokens 100))");
    check(legacy.kind == Expr::Kind::Op && legacy.op == OpKind::Or,
          "the legacy `any` is the variadic or");
    std::vector<std::string> refs;
    legacy.collect_refs(refs);
    check(refs.size() == 2 && refs[0] == "__status__.tree.terminals" && refs[1] == "__status__.tree.tokens",
          "and its bare scalars are qualified into __status__");
    check(legacy.stage() == Stage::Execute, "so it is an execution-stage predicate");
  }

  // --- identity ------------------------------------------------------------
  {
    auto a = Expr::conditional(zero_index, Expr::lit(0), Expr::lit(1));
    auto b = Expr::conditional(zero_index, Expr::lit(0), Expr::lit(1));
    auto c = Expr::conditional(zero_index, Expr::lit(1), Expr::lit(2));
    check(a.hash() == b.hash(), "the same expression hashes the same");
    check(a.hash() != c.hash(), "a different one does not");
    check(Expr::lit(1).hash() != Expr::lit(std::string("1")).hash(),
          "the value's type is part of its identity");
  }

  std::cout << (failures ? "FAILURES\n" : "all expression checks passed\n");
  return failures ? 1 : 0;
}
