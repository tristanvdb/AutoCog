#ifndef AUTOCOG_DATA_TERM_HXX
#define AUTOCOG_DATA_TERM_HXX

#include <string>
#include <vector>

namespace autocog::data {

/// A termination predicate: the boolean AST deciding when an evaluation stops
/// early. Leaves compare a named evaluation scalar (terminals, coverage.fta,
/// coverage.sta, best.proba, mean.proba, std.proba, best.zscore, tokens,
/// queue.size — wall time is deliberately excluded: nondeterministic, and
/// termination is hashed into program identity)
/// against a constant; inner nodes compose with all/any/not. Shared by
/// SearchConfig and FTA. Conversion lives in the free functions in
/// json.hxx / python.hxx.
struct TermExpr {
  enum class Kind { All, Any, Not, Ge, Gt, Le, Lt };

  Kind kind = Kind::All;
  std::vector<TermExpr> operands;   ///< All/Any: 1+; Not: exactly 1; comparisons: none.
  std::string scalar;               ///< Comparisons: the named scalar.
  float value = 0.0f;               ///< Comparisons: the constant compared against.

  /// Recursive content hash: digest(kind + scalar + value + operand hashes).
  std::string hash() const;

  /// Compact s-expression form, e.g. "(any (ge terminals 5) (lt tokens 100))".
  /// Used to carry a predicate through the (scalar, JSON-free) STA policy
  /// map; round-trips exactly.
  std::string to_compact() const;
  static TermExpr from_compact(std::string const & text);
};

}

#endif // AUTOCOG_DATA_TERM_HXX
