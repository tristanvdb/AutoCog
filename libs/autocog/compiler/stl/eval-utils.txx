
namespace autocog::compiler::stl::eval_utils {

// Static helper function for arithmetic operations
template<ast::OpKind Op>
ir::Value evaluateArithmetic(ir::Value const & lhs_val, ir::Value const & rhs_val,
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
          throw CompileError("Modulo by zero", loc);
        }
        return l % r;
      } else {
        throw CompileError("Modulo requires integer operands", loc);
      }
    }
    
    // General arithmetic for Add, Sub, Mul, Div
    if constexpr (std::is_same_v<L, int> && std::is_same_v<R, int>) {
      if constexpr (Op == ast::OpKind::Add) return l + r;
      else if constexpr (Op == ast::OpKind::Sub) return l - r;
      else if constexpr (Op == ast::OpKind::Mul) return l * r;
      else if constexpr (Op == ast::OpKind::Div) {
        if (r == 0) throw CompileError("Division by zero", loc);
        // Integer division promotes to float
        return static_cast<float>(l) / static_cast<float>(r);
      }
      else throw std::runtime_error("Invalid arithmetic operator: " + ast::opKindToString(Op));
    } 
    else if constexpr ((std::is_arithmetic_v<L> && std::is_arithmetic_v<R>) &&
                      (!std::is_same_v<L, bool> && !std::is_same_v<R, bool>)) {
      float lf = static_cast<float>(l);
      float rf = static_cast<float>(r);
      if constexpr (Op == ast::OpKind::Add) return lf + rf;
      else if constexpr (Op == ast::OpKind::Sub) return lf - rf;
      else if constexpr (Op == ast::OpKind::Mul) return lf * rf;
      else if constexpr (Op == ast::OpKind::Div) {
        if (rf == 0.0f) throw CompileError("Division by zero", loc);
        return lf / rf;
      }
      else throw std::runtime_error("Invalid arithmetic operator: " + ast::opKindToString(Op));
    } 
    else {
      if constexpr (Op == ast::OpKind::Add) {
        throw CompileError("Invalid types for addition", loc);
      } else if constexpr (Op == ast::OpKind::Sub) {
        throw CompileError("Invalid types for subtraction", loc);
      } else if constexpr (Op == ast::OpKind::Mul) {
        throw CompileError("Invalid types for multiplication", loc);
      } else if constexpr (Op == ast::OpKind::Div) {
        throw CompileError("Invalid types for division", loc);
      } else {
        throw CompileError("Invalid types for arithmetic operation", loc);
      }
    }
  }, lhs_val, rhs_val);
}

// Static helper function for comparison operations
template<ast::OpKind Op>
ir::Value evaluateComparison(ir::Value const & lhs_val, ir::Value const & rhs_val,
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
        throw CompileError("Invalid types for ordering comparison", loc);
      }
    }
  }, lhs_val, rhs_val);
}

// Static helper function for logical operations
template<ast::OpKind Op>
ir::Value evaluateLogical(ir::Value const & lhs_val, ir::Value const & rhs_val,
                                 std::optional<SourceRange> const & loc = std::nullopt) {
  return std::visit([&loc](auto const & l, auto const & r) -> ir::Value {
    using L = std::decay_t<decltype(l)>;
    using R = std::decay_t<decltype(r)>;
    
    if constexpr (std::is_same_v<L, bool> && std::is_same_v<R, bool>) {
      if constexpr (Op == ast::OpKind::And) return l && r;
      else if constexpr (Op == ast::OpKind::Or) return l || r;
      else throw std::runtime_error("Invalid logical operator: " + ast::opKindToString(Op));
    } else {
      if constexpr (Op == ast::OpKind::And) {
        throw CompileError("Logical AND requires boolean operands", loc);
      } else if constexpr (Op == ast::OpKind::Or) {
        throw CompileError("Logical OR requires boolean operands", loc);
      } else {
        throw std::runtime_error("Invalid logical operator: " + ast::opKindToString(Op));
      }
    }
  }, lhs_val, rhs_val);
}

} // namespace autocog::compiler

