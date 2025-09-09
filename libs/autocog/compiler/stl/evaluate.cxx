
#include "instantiate.hxx"

#include <stdexcept>

#if VERBOSE
#  include <iostream>
#endif

namespace autocog::compiler::stl {

static std::string opKindToString(ast::OpKind op) {
  switch (op) {
    case ast::OpKind::NOP: return "NOP";
    case ast::OpKind::Not: return "Not";
    case ast::OpKind::Neg: return "Neg";
    case ast::OpKind::Add: return "Add";
    case ast::OpKind::Sub: return "Sub";
    case ast::OpKind::Mul: return "Mul";
    case ast::OpKind::Div: return "Div";
    case ast::OpKind::Mod: return "Mod";
    case ast::OpKind::And: return "And";
    case ast::OpKind::Or: return "Or";
    case ast::OpKind::Lt: return "Lt";
    case ast::OpKind::Gt: return "Gt";
    case ast::OpKind::Lte: return "Lte";
    case ast::OpKind::Gte: return "Gte";
    case ast::OpKind::Eq: return "Eq";
    case ast::OpKind::Neq: return "Neq";
    default: return "Unknown(" + std::to_string(static_cast<int>(op)) + ")";
  }
}

// Exception for evaluation errors with source location
struct EvaluationError : std::exception {
  std::string message;
  std::optional<SourceRange> location;
  
  EvaluationError(std::string msg, std::optional<SourceRange> loc = std::nullopt) 
    : message(std::move(msg)), location(loc) {}
  
  const char* what() const noexcept override {
    return message.c_str();
  }
};

// Static helper function for arithmetic operations
template<ast::OpKind Op>
static ir::Value evaluateArithmetic(ir::Value const & lhs_val, ir::Value const & rhs_val,
                                    std::optional<SourceRange> const & loc = std::nullopt) {
  return std::visit([&loc](auto const & l, auto const & r) -> ir::Value {
    using L = std::decay_t<decltype(l)>;
    using R = std::decay_t<decltype(r)>;
    
    // Special case: string concatenation for Add
    if constexpr (Op == ast::OpKind::Add) {
      if constexpr (std::is_same_v<L, std::string> && std::is_same_v<R, std::string>) {
        return l + r;
      }
    }
    
    // Special case: Mod only works with integers
    if constexpr (Op == ast::OpKind::Mod) {
      if constexpr (std::is_same_v<L, int> && std::is_same_v<R, int>) {
        if (r == 0) {
          throw EvaluationError("Modulo by zero", loc);
        }
        return l % r;
      } else {
        throw EvaluationError("Modulo requires integer operands", loc);
      }
    }
    
    // General arithmetic for Add, Sub, Mul, Div
    if constexpr (std::is_same_v<L, int> && std::is_same_v<R, int>) {
      if constexpr (Op == ast::OpKind::Add) return l + r;
      else if constexpr (Op == ast::OpKind::Sub) return l - r;
      else if constexpr (Op == ast::OpKind::Mul) return l * r;
      else if constexpr (Op == ast::OpKind::Div) {
        if (r == 0) throw EvaluationError("Division by zero", loc);
        // Integer division promotes to float
        return static_cast<float>(l) / static_cast<float>(r);
      }
      else throw std::runtime_error("Invalid arithmetic operator: " + opKindToString(Op));
    } 
    else if constexpr ((std::is_arithmetic_v<L> && std::is_arithmetic_v<R>) &&
                      (!std::is_same_v<L, bool> && !std::is_same_v<R, bool>)) {
      float lf = static_cast<float>(l);
      float rf = static_cast<float>(r);
      if constexpr (Op == ast::OpKind::Add) return lf + rf;
      else if constexpr (Op == ast::OpKind::Sub) return lf - rf;
      else if constexpr (Op == ast::OpKind::Mul) return lf * rf;
      else if constexpr (Op == ast::OpKind::Div) {
        if (rf == 0.0f) throw EvaluationError("Division by zero", loc);
        return lf / rf;
      }
      else throw std::runtime_error("Invalid arithmetic operator: " + opKindToString(Op));
    } 
    else {
      if constexpr (Op == ast::OpKind::Add) {
        throw EvaluationError("Invalid types for addition", loc);
      } else if constexpr (Op == ast::OpKind::Sub) {
        throw EvaluationError("Invalid types for subtraction", loc);
      } else if constexpr (Op == ast::OpKind::Mul) {
        throw EvaluationError("Invalid types for multiplication", loc);
      } else if constexpr (Op == ast::OpKind::Div) {
        throw EvaluationError("Invalid types for division", loc);
      } else {
        throw EvaluationError("Invalid types for arithmetic operation", loc);
      }
    }
  }, lhs_val, rhs_val);
}

// Static helper function for comparison operations
template<ast::OpKind Op>
static ir::Value evaluateComparison(ir::Value const & lhs_val, ir::Value const & rhs_val,
                                    std::optional<SourceRange> const & loc = std::nullopt) {
  return std::visit([&loc](auto const & l, auto const & r) -> ir::Value {
    using L = std::decay_t<decltype(l)>;
    using R = std::decay_t<decltype(r)>;
    
    // Equality and inequality work for all type combinations
    if constexpr (Op == ast::OpKind::Eq || Op == ast::OpKind::Neq) {
      if constexpr (std::is_same_v<L, R>) {
        if constexpr (Op == ast::OpKind::Eq) return l == r;
        else return l != r;
      } 
      else if constexpr ((std::is_arithmetic_v<L> && std::is_arithmetic_v<R>) &&
                        (!std::is_same_v<L, bool> && !std::is_same_v<R, bool>)) {
        // Allow numeric comparison between int and float
        double ld = static_cast<double>(l);
        double rd = static_cast<double>(r);
        if constexpr (Op == ast::OpKind::Eq) return ld == rd;
        else return ld != rd;
      } 
      else {
        // Different types: always false for ==, always true for !=
        if constexpr (Op == ast::OpKind::Eq) return false;
        else return true;
      }
    }
    // Ordering comparisons only work for numbers and strings
    else {
      if constexpr ((std::is_arithmetic_v<L> && std::is_arithmetic_v<R>) &&
                   (!std::is_same_v<L, bool> && !std::is_same_v<R, bool>)) {
        double ld = static_cast<double>(l);
        double rd = static_cast<double>(r);
        if constexpr (Op == ast::OpKind::Lt) return ld < rd;
        else if constexpr (Op == ast::OpKind::Lte) return ld <= rd;
        else if constexpr (Op == ast::OpKind::Gt) return ld > rd;
        else if constexpr (Op == ast::OpKind::Gte) return ld >= rd;
        else throw std::runtime_error("Invalid comparison operator");
      } 
      else if constexpr (std::is_same_v<L, std::string> && std::is_same_v<R, std::string>) {
        if constexpr (Op == ast::OpKind::Lt) return l < r;
        else if constexpr (Op == ast::OpKind::Lte) return l <= r;
        else if constexpr (Op == ast::OpKind::Gt) return l > r;
        else if constexpr (Op == ast::OpKind::Gte) return l >= r;
        else throw std::runtime_error("Invalid comparison operator");
      } 
      else {
        throw EvaluationError("Invalid types for ordering comparison", loc);
      }
    }
  }, lhs_val, rhs_val);
}

// Static helper function for logical operations
template<ast::OpKind Op>
static ir::Value evaluateLogical(ir::Value const & lhs_val, ir::Value const & rhs_val,
                                 std::optional<SourceRange> const & loc = std::nullopt) {
  return std::visit([&loc](auto const & l, auto const & r) -> ir::Value {
    using L = std::decay_t<decltype(l)>;
    using R = std::decay_t<decltype(r)>;
    
    if constexpr (std::is_same_v<L, bool> && std::is_same_v<R, bool>) {
      if constexpr (Op == ast::OpKind::And) return l && r;
      else if constexpr (Op == ast::OpKind::Or) return l || r;
      else throw std::runtime_error("Invalid logical operator: " + opKindToString(Op));
    } else {
      if constexpr (Op == ast::OpKind::And) {
        throw EvaluationError("Logical AND requires boolean operands", loc);
      } else if constexpr (Op == ast::OpKind::Or) {
        throw EvaluationError("Logical OR requires boolean operands", loc);
      } else {
        throw std::runtime_error("Invalid logical operator: " + opKindToString(Op));
      }
    }
  }, lhs_val, rhs_val);
}

ir::Value Instantiator::evaluateIdentifier(ast::Program const & program, ast::Identifier const & id, ir::VarMap & varmap) {
  auto it = varmap.find(id.data.name);
  if (it == varmap.end()) {
    if (!define_one(program, id.data.name, varmap)) {
      throw EvaluationError("Could not find symbol for " + id.data.name, id.location);
    }
    it = varmap.find(id.data.name);
    if (it == varmap.end()) {
      throw std::runtime_error("");
    }
  }
  return it->second;
}

ir::Value Instantiator::evaluateUnaryOp(ast::Program const & program, ast::Unary const & op, ir::VarMap & varmap) {
  ir::Value operand_val = evaluate(program, *op.data.operand, varmap);
  
  switch (op.data.kind) {
    case ast::OpKind::Neg:
      return std::visit([&op](auto const & v) -> ir::Value {
        using V = std::decay_t<decltype(v)>;
        if constexpr (std::is_same_v<V, int>) {
          return -v;
        } else if constexpr (std::is_same_v<V, float>) {
          return -v;
        } else {
          throw EvaluationError("Cannot negate non-numeric value", op.location);
        }
      }, operand_val);
      
    case ast::OpKind::Not:
      return std::visit([&op](auto const & v) -> ir::Value {
        using V = std::decay_t<decltype(v)>;
        if constexpr (std::is_same_v<V, bool>) {
          return !v;
        } else {
          throw EvaluationError("Cannot apply 'not' to non-boolean value", op.location);
        }
      }, operand_val);
      
    default:
      throw std::runtime_error("Invalid unary operator kind: " + opKindToString(op.data.kind));
  }
}

ir::Value Instantiator::evaluateBinaryOp(ast::Program const & program, ast::Binary const & op, ir::VarMap & varmap) {
  ir::Value lhs_val = evaluate(program, *op.data.lhs, varmap);
  ir::Value rhs_val = evaluate(program, *op.data.rhs, varmap);
  
  switch (op.data.kind) {
    // Arithmetic operators
    case ast::OpKind::Add:
      return evaluateArithmetic<ast::OpKind::Add>(lhs_val, rhs_val, op.location);
    case ast::OpKind::Sub:
      return evaluateArithmetic<ast::OpKind::Sub>(lhs_val, rhs_val, op.location);
    case ast::OpKind::Mul:
      return evaluateArithmetic<ast::OpKind::Mul>(lhs_val, rhs_val, op.location);
    case ast::OpKind::Div:
      return evaluateArithmetic<ast::OpKind::Div>(lhs_val, rhs_val, op.location);
    case ast::OpKind::Mod:
      return evaluateArithmetic<ast::OpKind::Mod>(lhs_val, rhs_val, op.location);
      
    // Comparison operators
    case ast::OpKind::Eq:
      return evaluateComparison<ast::OpKind::Eq>(lhs_val, rhs_val, op.location);
    case ast::OpKind::Neq:
      return evaluateComparison<ast::OpKind::Neq>(lhs_val, rhs_val, op.location);
    case ast::OpKind::Lt:
      return evaluateComparison<ast::OpKind::Lt>(lhs_val, rhs_val, op.location);
    case ast::OpKind::Lte:
      return evaluateComparison<ast::OpKind::Lte>(lhs_val, rhs_val, op.location);
    case ast::OpKind::Gt:
      return evaluateComparison<ast::OpKind::Gt>(lhs_val, rhs_val, op.location);
    case ast::OpKind::Gte:
      return evaluateComparison<ast::OpKind::Gte>(lhs_val, rhs_val, op.location);
      
    // Logical operators
    case ast::OpKind::And:
      return evaluateLogical<ast::OpKind::And>(lhs_val, rhs_val, op.location);
    case ast::OpKind::Or:
      return evaluateLogical<ast::OpKind::Or>(lhs_val, rhs_val, op.location);
      
    default:
      throw std::runtime_error("Invalid binary operator kind");
  }
}

ir::Value Instantiator::evaluateConditionalOp(ast::Program const & program, ast::Conditional const & op, ir::VarMap & varmap) {
  ir::Value cond_val = evaluate(program, *op.data.cond, varmap);
  
  bool condition = std::visit([&op](auto const & v) -> bool {
    using V = std::decay_t<decltype(v)>;
    if constexpr (std::is_same_v<V, bool>) {
      return v;
    } else {
      throw EvaluationError("Conditional expression requires boolean condition", op.location);
    }
  }, cond_val);
  
  if (condition) {
    return evaluate(program, *op.data.e_true, varmap);
  } else {
    return evaluate(program, *op.data.e_false, varmap);
  }
}

ir::Value Instantiator::evaluateParens(ast::Program const & program, ast::Parenthesis const & parens, ir::VarMap & varmap) {
  return evaluate(program, *parens.data.expr, varmap);
}

std::string Instantiator::formatString(ast::Program const & program, ast::String const & fstring, ir::VarMap & varmap) {
  if (!fstring.data.is_format) {
    return fstring.data.value;
  }
  
  const std::string & fmt = fstring.data.value;
  std::string result;
  result.reserve(fmt.length() * 1.5); // Reserve some extra space
  
  size_t i = 0;
  while (i < fmt.length()) {
    // Handle escaped braces
    if (i + 1 < fmt.length()) {
      if (fmt[i] == '{' && fmt[i + 1] == '{') {
        result += '{';
        i += 2;
        continue;
      }
      if (fmt[i] == '}' && fmt[i + 1] == '}') {
        result += '}';
        i += 2;
        continue;
      }
    }
    
    // Handle variable substitution
    if (fmt[i] == '{') {
      // Find closing brace
      size_t j = i + 1;
      while (j < fmt.length() && fmt[j] != '}') {
        j++;
      }
      
      if (j >= fmt.length()) {
        throw EvaluationError("Unclosed '{' in format string", fstring.location);
      }
      
      // Extract variable name
      std::string var_name = fmt.substr(i + 1, j - i - 1);
      
      // Validate it's a valid identifier
      if (var_name.empty()) {
        throw EvaluationError("Empty variable name in format string", fstring.location);
      }
      
      // Simple identifier validation (first char is letter or _, rest are alphanumeric or _)
      if (!std::isalpha(var_name[0]) && var_name[0] != '_') {
        throw EvaluationError("Invalid variable name in format string: " + var_name, fstring.location);
      }
      for (size_t k = 1; k < var_name.length(); k++) {
        if (!std::isalnum(var_name[k]) && var_name[k] != '_') {
          throw EvaluationError("Invalid variable name in format string: " + var_name, fstring.location);
        }
      }
      
      // Look up variable
      auto it = varmap.find(var_name);
      if (it == varmap.end()) {
        if (!define_one(program, var_name, varmap)) {
          throw EvaluationError("Undefined variable in format string: " + var_name, fstring.location);
        }
        it = varmap.find(var_name);
        if (it == varmap.end()) {
          throw std::runtime_error("Inconsistency expect to find the value for " + var_name);
        }
      }
      
      // Convert value to string and append
      result += std::visit([](auto const & v) -> std::string {
        using V = std::decay_t<decltype(v)>;
        if constexpr (std::is_same_v<V, int>) {
          return std::to_string(v);
        } else if constexpr (std::is_same_v<V, float>) {
          // Format float to avoid unnecessary trailing zeros
          std::string s = std::to_string(v);
          // Remove trailing zeros after decimal point
          if (s.find('.') != std::string::npos) {
            s.erase(s.find_last_not_of('0') + 1, std::string::npos);
            // Remove decimal point if it's the last character
            if (s.back() == '.') {
              s.pop_back();
            }
          }
          return s;
        } else if constexpr (std::is_same_v<V, bool>) {
          return v ? "true" : "false";
        } else if constexpr (std::is_same_v<V, std::string>) {
          return v;
        } else {
          return "<unknown>";
        }
      }, it->second);
      
      i = j + 1;
    }
    // Handle stray closing brace
    else if (fmt[i] == '}') {
      throw EvaluationError("Unmatched '}' in format string", fstring.location);
    }
    // Regular character
    else {
      result += fmt[i];
      i++;
    }
  }
  
  return result;
}

ir::Value Instantiator::evaluate(ast::Program const & program, ast::Expression const & expr, ir::VarMap & varmap) {
  try {
    return std::visit([&](auto const & e) -> ir::Value {
      using T = std::decay_t<decltype(e)>;
      
      // Handle literals directly
      if constexpr (std::is_same_v<T, ast::Integer>) {
        return e.data.value;
      }
      else if constexpr (std::is_same_v<T, ast::Float>) {
        return e.data.value;
      }
      else if constexpr (std::is_same_v<T, ast::Boolean>) {
        return e.data.value;
      }
      else if constexpr (std::is_same_v<T, ast::String>) {
        return formatString(program, e, varmap);
      }
      // Delegate complex cases to helper functions
      else if constexpr (std::is_same_v<T, ast::Identifier>) {
        return evaluateIdentifier(program, e, varmap);
      }
      else if constexpr (std::is_same_v<T, ast::Unary>) {
        return evaluateUnaryOp(program, e, varmap);
      }
      else if constexpr (std::is_same_v<T, ast::Binary>) {
        return evaluateBinaryOp(program, e, varmap);
      }
      else if constexpr (std::is_same_v<T, ast::Conditional>) {
        return evaluateConditionalOp(program, e, varmap);
      }
      else if constexpr (std::is_same_v<T, ast::Parenthesis>) {
        return evaluateParens(program, e, varmap);
      }
      else {
        throw std::runtime_error("Unknown expression variant type");
      }
    }, expr.data.expr);
  } catch (EvaluationError const & e) {
    emit_error(e.message, e.location);
    // Return a default value - ideally we'd know the expected type here
    // For now, return 0 as a generic default
    return 0;
  }
  // Let std::runtime_error propagate for impossible cases
}

} // namespace autocog::compiler

