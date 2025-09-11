
namespace autocog::compiler::stl {

template <class ScopeT>
ir::Value Instantiator::evaluate(ScopeT const & scope, ast::Unary const & op, ir::VarMap & varmap) {
  ir::Value operand_val = evaluate(scope, *op.data.operand, varmap);
  
  switch (op.data.kind) {
    case ast::OpKind::Neg:
      return std::visit([&op](auto const & v) -> ir::Value {
        using V = std::decay_t<decltype(v)>;
        if constexpr (std::is_same_v<V, int>) {
          return -v;
        } else if constexpr (std::is_same_v<V, float>) {
          return -v;
        } else {
          throw CompileError("Cannot negate non-numeric value", op.location);
        }
      }, operand_val);
      
    case ast::OpKind::Not:
      return std::visit([&op](auto const & v) -> ir::Value {
        using V = std::decay_t<decltype(v)>;
        if constexpr (std::is_same_v<V, bool>) {
          return !v;
        } else {
          throw CompileError("Cannot apply 'not' to non-boolean value", op.location);
        }
      }, operand_val);
      
    default:
      throw std::runtime_error("Invalid unary operator kind: " + ast::opKindToString(op.data.kind));
  }
}

template <class ScopeT>
ir::Value Instantiator::evaluate(ScopeT const & scope, ast::Binary const & op, ir::VarMap & varmap) {
  ir::Value lhs_val = evaluate(scope, *op.data.lhs, varmap);
  ir::Value rhs_val = evaluate(scope, *op.data.rhs, varmap);
  
  switch (op.data.kind) {
    // Arithmetic operators
    case ast::OpKind::Add:
      return eval_utils::evaluateArithmetic<ast::OpKind::Add>(lhs_val, rhs_val, op.location);
    case ast::OpKind::Sub:
      return eval_utils::evaluateArithmetic<ast::OpKind::Sub>(lhs_val, rhs_val, op.location);
    case ast::OpKind::Mul:
      return eval_utils::evaluateArithmetic<ast::OpKind::Mul>(lhs_val, rhs_val, op.location);
    case ast::OpKind::Div:
      return eval_utils::evaluateArithmetic<ast::OpKind::Div>(lhs_val, rhs_val, op.location);
    case ast::OpKind::Mod:
      return eval_utils::evaluateArithmetic<ast::OpKind::Mod>(lhs_val, rhs_val, op.location);
      
    // Comparison operators
    case ast::OpKind::Eq:
      return eval_utils::evaluateComparison<ast::OpKind::Eq>(lhs_val, rhs_val, op.location);
    case ast::OpKind::Neq:
      return eval_utils::evaluateComparison<ast::OpKind::Neq>(lhs_val, rhs_val, op.location);
    case ast::OpKind::Lt:
      return eval_utils::evaluateComparison<ast::OpKind::Lt>(lhs_val, rhs_val, op.location);
    case ast::OpKind::Lte:
      return eval_utils::evaluateComparison<ast::OpKind::Lte>(lhs_val, rhs_val, op.location);
    case ast::OpKind::Gt:
      return eval_utils::evaluateComparison<ast::OpKind::Gt>(lhs_val, rhs_val, op.location);
    case ast::OpKind::Gte:
      return eval_utils::evaluateComparison<ast::OpKind::Gte>(lhs_val, rhs_val, op.location);
      
    // Logical operators
    case ast::OpKind::And:
      return eval_utils::evaluateLogical<ast::OpKind::And>(lhs_val, rhs_val, op.location);
    case ast::OpKind::Or:
      return eval_utils::evaluateLogical<ast::OpKind::Or>(lhs_val, rhs_val, op.location);
      
    default:
      throw std::runtime_error("Invalid binary operator kind");
  }
}

template <class ScopeT>
ir::Value Instantiator::evaluate(ScopeT const & scope, ast::Conditional const & op, ir::VarMap & varmap) {
  ir::Value cond_val = evaluate(scope, *op.data.cond, varmap);
  
  bool condition = std::visit([&op](auto const & v) -> bool {
    using V = std::decay_t<decltype(v)>;
    if constexpr (std::is_same_v<V, bool>) {
      return v;
    } else {
      throw CompileError("Conditional expression requires boolean condition", op.location);
    }
  }, cond_val);
  
  if (condition) {
    return evaluate(scope, *op.data.e_true, varmap);
  } else {
    return evaluate(scope, *op.data.e_false, varmap);
  }
}

template <class ScopeT>
ir::Value Instantiator::evaluate(ScopeT const & scope, ast::String const & fstring, ir::VarMap & varmap) {
  if (!fstring.data.is_format) {
    return fstring.data.value;
  }
  
  const std::string & fmt = fstring.data.value;
  std::string result;
  result.reserve(fmt.length() * 1.5);
  
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
        throw CompileError("Unclosed '{' in format string", fstring.location);
      }
      
      // Extract variable name
      std::string var_name = fmt.substr(i + 1, j - i - 1);
      
      // Validate it's a valid identifier
      if (var_name.empty()) {
        throw CompileError("Empty variable name in format string", fstring.location);
      }
      
      // Simple identifier validation (first char is letter or _, rest are alphanumeric or _)
      if (!std::isalpha(var_name[0]) && var_name[0] != '_') {
        throw CompileError("Invalid variable name in format string: " + var_name, fstring.location);
      }
      for (size_t k = 1; k < var_name.length(); k++) {
        if (!std::isalnum(var_name[k]) && var_name[k] != '_') {
          throw CompileError("Invalid variable name in format string: " + var_name, fstring.location);
        }
      }
      
      // Look up variable
      ir::Value value = retrieve_value(scope, var_name, varmap);
      
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
      }, value);
      
      i = j + 1;
    } else if (fmt[i] == '}') {
      throw CompileError("Unmatched '}' in format string", fstring.location);
    } else {
      result += fmt[i];
      i++;
    }
  }
  
  return result;
}

template <class ScopeT>
ir::Value Instantiator::evaluate(ScopeT const & scope, ast::Expression const & expr, ir::VarMap & varmap) {
  return std::visit([&](auto const & e) -> ir::Value {
    using T = std::decay_t<decltype(e)>;
    if constexpr (std::is_same_v<T, ast::Integer>) {
      return e.data.value;
    } else if constexpr (std::is_same_v<T, ast::Float>) {
      return e.data.value;
    } else if constexpr (std::is_same_v<T, ast::Boolean>) {
      return e.data.value;
    } else if constexpr (std::is_same_v<T, ast::String>) {
      return evaluate(scope, e, varmap);
    } else if constexpr (std::is_same_v<T, ast::Identifier>) {
      return retrieve_value(scope, e.data.name, varmap);
    } else if constexpr (std::is_same_v<T, ast::Unary>) {
      return evaluate(scope, e, varmap);
    } else if constexpr (std::is_same_v<T, ast::Binary>) {
      return evaluate(scope, e, varmap);
    } else if constexpr (std::is_same_v<T, ast::Conditional>) {
      return evaluate(scope, e, varmap);
    } else if constexpr (std::is_same_v<T, ast::Parenthesis>) {
      return evaluate(scope, *(e.data.expr), varmap);
    } else {
      throw std::runtime_error("Unknown expression variant type");
    }
  }, expr.data.expr);
}

#define DEBUG_Instantiator_retrieve_value VERBOSE && 0

template <class ScopeT>
ir::Value Instantiator::retrieve_value(
  ScopeT const & scope,
  std::string const & varname,
  ir::VarMap & varmap,
  std::optional<SourceRange> const & loc
) {
#if DEBUG_Instantiator_retrieve_value
  std::cerr << "Instantiator::retrieve_value(" << varname << ")" << std::endl;
#endif
  ir::Value value = nullptr;

  auto varmap_it = varmap.find(varname);
  if (varmap_it != varmap.end()) {
    value = varmap_it->second;
  } else {
    auto it = scope.data.defines.find(varname);
    if (it != scope.data.defines.end()) {
      auto const & defn = it->second;
      
      if (defn.data.argument) {
        throw std::runtime_error("Argument `" + varname + "` should have been found in the variable map.");
      }

      if (!defn.data.init) {
        throw CompileError("Define `" + varname + "` has no initializer to evaluate.", loc);
      }
      
      varmap[varname] = nullptr; // causes cycles to return error values
      value = evaluate(scope, defn.data.init.value(), varmap);
      varmap[varname] = value;
    }
  }
  if (std::holds_alternative<std::nullptr_t>(value)) {
    throw CompileError(
              "Found error value when retriving variable `" + varname + "`." +
                "If no previous error was reported then it is likely a circular " +
                "dependency between variable initializers.",
              loc
          );
  }
  return value;
}

} // namespace autocog::compiler

