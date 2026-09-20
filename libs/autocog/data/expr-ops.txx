#ifndef AUTOCOG_DATA_EXPR_OPS_TXX
#define AUTOCOG_DATA_EXPR_OPS_TXX

// Operator semantics for the expression language: one definition, used by
// compile-time folding (stlc), by instantiation (ista) and by execution
// (xfta). They were written for the STL evaluator and moved here unchanged
// when the expression language became shared -- a second implementation is
// exactly how a constant-folded `1/0` and a deferred one start disagreeing.
//
// Included by expr.cxx, which wraps them in the runtime dispatch
// apply_unary/apply_binary. The templates stay templates so the operator is a
// compile-time constant inside the visit, which is what lets the error
// messages name the operator without a table.

#include "autocog/data/expr.hxx"
#include "autocog/utilities/exception.hxx"

#include <optional>
#include <string>
#include <type_traits>
#include <variant>


namespace autocog::data::ops {

// Static helper function for arithmetic operations
template<OpKind Op>
Value evaluateArithmetic(Value const & lhs_val, Value const & rhs_val,
                                    std::optional<autocog::location::SourceRange> const & loc = std::nullopt) {
  return std::visit([&loc](auto const & l, auto const & r) -> Value {
    using L = std::decay_t<decltype(l)>;
    using R = std::decay_t<decltype(r)>;
    
    // Special case: string concatenation for Add
    if constexpr (Op == OpKind::Add) {
      if constexpr (std::is_same_v<L, std::string> && std::is_same_v<R, std::string>) {
        return l + r;
      }
    }
    
    // Special case: Mod only works with integers
    if constexpr (Op == OpKind::Mod) {
      if constexpr (std::is_same_v<L, int> && std::is_same_v<R, int>) {
        if (r == 0) {
          throw CompileError("Modulo by zero", loc);
        }
        return l % r;
      } else {
        throw CompileError("Modulo requires integer operands", loc);
      }
    }
    
    // General arithmetic for Add, Sub, Mul, Div
    if constexpr (std::is_same_v<L, int> && std::is_same_v<R, int>) {
      if constexpr (Op == OpKind::Add) return l + r;
      else if constexpr (Op == OpKind::Sub) return l - r;
      else if constexpr (Op == OpKind::Mul) return l * r;
      else if constexpr (Op == OpKind::Div) {
        if (r == 0) throw CompileError("Division by zero", loc);
        // Integer division promotes to float
        return static_cast<float>(l) / static_cast<float>(r);
      }
      else throw autocog::utilities::InternalError("Invalid arithmetic operator: " + opKindToString(Op));
    } 
    else if constexpr ((std::is_arithmetic_v<L> && std::is_arithmetic_v<R>) &&
                      (!std::is_same_v<L, bool> && !std::is_same_v<R, bool>)) {
      float lf = static_cast<float>(l);
      float rf = static_cast<float>(r);
      if constexpr (Op == OpKind::Add) return lf + rf;
      else if constexpr (Op == OpKind::Sub) return lf - rf;
      else if constexpr (Op == OpKind::Mul) return lf * rf;
      else if constexpr (Op == OpKind::Div) {
        if (rf == 0.0f) throw CompileError("Division by zero", loc);
        return lf / rf;
      }
      else throw autocog::utilities::InternalError("Invalid arithmetic operator: " + opKindToString(Op));
    } 
    else {
      if constexpr (Op == OpKind::Add) {
        throw CompileError("Invalid types for addition", loc);
      } else if constexpr (Op == OpKind::Sub) {
        throw CompileError("Invalid types for subtraction", loc);
      } else if constexpr (Op == OpKind::Mul) {
        throw CompileError("Invalid types for multiplication", loc);
      } else if constexpr (Op == OpKind::Div) {
        throw CompileError("Invalid types for division", loc);
      } else {
        throw CompileError("Invalid types for arithmetic operation", loc);
      }
    }
  }, lhs_val, rhs_val);
}

// Static helper for bitwise operations (|, &). Integer operands only; any other
// operand type (float, string, bool) is a type error. Bool is excluded on
// purpose: logical and/or is spelled && / || -- there must be one way to say it.
template<OpKind Op>
Value evaluateBitwise(Value const & lhs_val, Value const & rhs_val,
                                 std::optional<autocog::location::SourceRange> const & loc = std::nullopt) {
  return std::visit([&loc](auto const & l, auto const & r) -> Value {
    using L = std::decay_t<decltype(l)>;
    using R = std::decay_t<decltype(r)>;
    if constexpr (std::is_same_v<L, int> && std::is_same_v<R, int>) {
      if constexpr (Op == OpKind::BOr) return l | r;
      else if constexpr (Op == OpKind::BAnd) return l & r;
      else throw autocog::utilities::InternalError("Invalid bitwise operator: " + opKindToString(Op));
    } else {
      if constexpr (Op == OpKind::BOr) {
        throw CompileError("Operator `|` requires integer operands", loc);
      } else {
        throw CompileError("Operator `&` requires integer operands", loc);
      }
    }
  }, lhs_val, rhs_val);
}

// Static helper function for comparison operations
template<OpKind Op>
Value evaluateComparison(Value const & lhs_val, Value const & rhs_val,
                                    std::optional<autocog::location::SourceRange> const & loc = std::nullopt) {
  return std::visit([&loc](auto const & l, auto const & r) -> Value {
    using L = std::decay_t<decltype(l)>;
    using R = std::decay_t<decltype(r)>;
    
    // Equality and inequality work for all type combinations
    if constexpr (Op == OpKind::Eq || Op == OpKind::Neq) {
      if constexpr (std::is_same_v<L, R>) {
        if constexpr (Op == OpKind::Eq) return l == r;
        else return l != r;
      } 
      else if constexpr ((std::is_arithmetic_v<L> && std::is_arithmetic_v<R>) &&
                        (!std::is_same_v<L, bool> && !std::is_same_v<R, bool>)) {
        // Allow numeric comparison between int and float
        double ld = static_cast<double>(l);
        double rd = static_cast<double>(r);
        if constexpr (Op == OpKind::Eq) return ld == rd;
        else return ld != rd;
      } 
      else {
        // Different types: always false for ==, always true for !=
        if constexpr (Op == OpKind::Eq) return false;
        else return true;
      }
    }
    // Ordering comparisons only work for numbers and strings
    else {
      if constexpr ((std::is_arithmetic_v<L> && std::is_arithmetic_v<R>) &&
                   (!std::is_same_v<L, bool> && !std::is_same_v<R, bool>)) {
        double ld = static_cast<double>(l);
        double rd = static_cast<double>(r);
        if constexpr (Op == OpKind::Lt) return ld < rd;
        else if constexpr (Op == OpKind::Lte) return ld <= rd;
        else if constexpr (Op == OpKind::Gt) return ld > rd;
        else if constexpr (Op == OpKind::Gte) return ld >= rd;
        else throw autocog::utilities::InternalError("Invalid comparison operator");
      } 
      else if constexpr (std::is_same_v<L, std::string> && std::is_same_v<R, std::string>) {
        if constexpr (Op == OpKind::Lt) return l < r;
        else if constexpr (Op == OpKind::Lte) return l <= r;
        else if constexpr (Op == OpKind::Gt) return l > r;
        else if constexpr (Op == OpKind::Gte) return l >= r;
        else throw autocog::utilities::InternalError("Invalid comparison operator");
      } 
      else {
        throw CompileError("Invalid types for ordering comparison", loc);
      }
    }
  }, lhs_val, rhs_val);
}

// Static helper function for logical operations
template<OpKind Op>
Value evaluateLogical(Value const & lhs_val, Value const & rhs_val,
                                 std::optional<autocog::location::SourceRange> const & loc = std::nullopt) {
  return std::visit([&loc](auto const & l, auto const & r) -> Value {
    using L = std::decay_t<decltype(l)>;
    using R = std::decay_t<decltype(r)>;
    
    if constexpr (std::is_same_v<L, bool> && std::is_same_v<R, bool>) {
      if constexpr (Op == OpKind::And) return l && r;
      else if constexpr (Op == OpKind::Or) return l || r;
      else throw autocog::utilities::InternalError("Invalid logical operator: " + opKindToString(Op));
    } else {
      if constexpr (Op == OpKind::And) {
        throw CompileError("Logical AND requires boolean operands", loc);
      } else if constexpr (Op == OpKind::Or) {
        throw CompileError("Logical OR requires boolean operands", loc);
      } else {
        throw autocog::utilities::InternalError("Invalid logical operator: " + opKindToString(Op));
      }
    }
  }, lhs_val, rhs_val);
}

} // namespace autocog::data::ops


#endif // AUTOCOG_DATA_EXPR_OPS_TXX
