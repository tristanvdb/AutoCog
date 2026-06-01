
#include "evaluate.hxx"
#include "autocog/logging.hxx"
#include "autocog/utilities/exception.hxx"

#include "autocog/compiler/stl/symbol-table.hxx"

#include <stdexcept>


namespace autocog::compiler::stl {

Evaluator::Evaluator(std::list<Diagnostic> & diagnostics_, SymbolTable & tables_) :
  diagnostics(diagnostics_),
  tables(tables_)
{}

ir::Value Evaluator::evaluate(std::string const & scope, ast::Unary const & op, ir::VarMap & varmap) {
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
      throw autocog::utilities::InternalError("Invalid unary operator kind: " + ast::opKindToString(op.data.kind));
  }
}

ir::Value Evaluator::evaluate(std::string const & scope, ast::Binary const & op, ir::VarMap & varmap) {
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
      throw autocog::utilities::InternalError("Invalid binary operator kind");
  }
}

ir::Value Evaluator::evaluate(std::string const & scope, ast::Conditional const & op, ir::VarMap & varmap) {
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

ir::Value Evaluator::evaluate(std::string const & scope, ast::String const & fstring, ir::VarMap & varmap) {
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
      ir::Value value = retrieve_value(scope, var_name, varmap, fstring.location);
      
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

ir::Value Evaluator::evaluate(std::string const & scope, ast::Expression const & expr, ir::VarMap & varmap) {
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
      return retrieve_value(scope, e.data.name, varmap, e.location);
    } else if constexpr (std::is_same_v<T, ast::Unary>) {
      return evaluate(scope, e, varmap);
    } else if constexpr (std::is_same_v<T, ast::Binary>) {
      return evaluate(scope, e, varmap);
    } else if constexpr (std::is_same_v<T, ast::Conditional>) {
      return evaluate(scope, e, varmap);
    } else if constexpr (std::is_same_v<T, ast::Parenthesis>) {
      return evaluate(scope, *(e.data.expr), varmap);
    } else {
      throw autocog::utilities::InternalError("Unknown expression variant type");
    }
  }, expr.data.expr);
}


ir::Value Evaluator::retrieve_value(
  std::string const & scope,
  std::string const & varname,
  ir::VarMap & varmap,
  std::optional<SourceRange> const & loc
) {
  SPDLOG_LOGGER_TRACE(autocog::log(), "Evaluator::retrieve_value( )");
  ir::Value value = nullptr;

  auto varmap_it = varmap.find(varname);
  if (varmap_it != varmap.end()) {
    value = varmap_it->second;
  } else {
    auto sym_it = this->tables.symbols.find(scope+"::"+varname);
    if (sym_it == this->tables.symbols.end()) {
      // Try parent scope (file scope if we're in a nested scope like "0::Container")
      std::string parent_scope = scope;
      auto last_sep = parent_scope.rfind("::");
      if (last_sep != std::string::npos) {
        parent_scope = parent_scope.substr(0, last_sep);
        sym_it = this->tables.symbols.find(parent_scope+"::"+varname);
      }

      // If still not found, throw error
      if (sym_it == this->tables.symbols.end()) {
        throw CompileError("Undefined symbol: " + varname, loc);
      }
    }
    auto const & sym = sym_it->second;
    if (std::holds_alternative<DefineSymbol>(sym)) {
      auto const & defn = std::get<DefineSymbol>(sym).node;
      
      if (defn.data.kind == ast::DefineKind::Argument) {
        throw autocog::utilities::InternalError("Argument `" + varname + "` should have been found in the variable map.");
      }
      if (defn.data.kind == ast::DefineKind::Vocab) {
        throw CompileError("Vocab `" + varname + "` cannot be used as a value.", loc);
      }

      if (!defn.data.init) {
        throw CompileError("Define `" + varname + "` has no initializer to evaluate.", loc);
      }
      
      varmap[varname] = nullptr; // causes cycles to return error values
      value = evaluate(scope, defn.data.init.value(), varmap);
      varmap[varname] = value;
      
    } else {
      // FIXME could look up the value in the parent scope but do we authorize shadowing of globals by object of different kind?
      throw CompileError("Found a symbol for another object than a define when looking up " + varname + " for evaluation!", loc);
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

ir::Value Evaluator::evaluate_expression(
  std::string const & scope,
  ast::Expression const & expr,
  ir::VarMap & varmap
) {
  return evaluate(scope, expr, varmap);
}

// ---------------------------------------------------------------------------
// Vocab translation
// ---------------------------------------------------------------------------

namespace {
// A memoization key for a vocab resolution: the vocab's name plus a serialized
// form of the context that affects its f-strings. Identical (name, ctx) -> same
// key; different ctx -> different key (so context-dependent vocabs stay
// distinct across instantiations). Need not byte-match the graph's mangling;
// it is an internal memo namespace.
std::string vocab_key(std::string const & name, ir::VarMap const & ctx) {
  std::string k = name;
  std::vector<std::string> keys;
  for (auto const & [kk, _] : ctx) keys.push_back(kk);
  std::sort(keys.begin(), keys.end());
  for (auto const & kk : keys) {
    k += "|" + kk + "=";
    std::visit([&](auto const & v) {
      using T = std::decay_t<decltype(v)>;
      if constexpr (std::is_same_v<T, std::string>) k += v;
      else if constexpr (std::is_same_v<T, std::nullptr_t>) k += "null";
      else if constexpr (std::is_same_v<T, bool>) k += (v ? "true" : "false");
      else k += std::to_string(v);
    }, ctx.at(kk));
  }
  return k;
}
} // namespace

std::unique_ptr<ir::VocabExpr> Evaluator::translate_vocab(
  std::string const & scope, ast::Expression const & expr, ir::VarMap & ctx,
  std::optional<SourceRange> const & loc
) {
  auto eval_str = [&](ast::String const & s) -> std::string {
    ir::Value v = evaluate(scope, s, ctx);
    if (auto const * sv = std::get_if<std::string>(&v)) return *sv;
    throw CompileError("vocab string argument did not evaluate to a string.", loc);
    return "";
  };

  return std::visit([&](auto const & node) -> std::unique_ptr<ir::VocabExpr> {
    using N = std::decay_t<decltype(node)>;
    if constexpr (std::is_same_v<N, std::monostate>) {
      throw CompileError("empty vocab expression.", loc);
      return nullptr;
    } else {
      constexpr ast::Tag tag = N::tag;
      auto out = std::make_unique<ir::VocabExpr>();
      if constexpr (tag == ast::Tag::Tokenize) {
        out->kind = ir::VocabExpr::Kind::Tokenize;
        for (auto const & s : node.data.strings) out->strings.push_back(eval_str(s));
        return out;
      } else if constexpr (tag == ast::Tag::Regex) {
        out->kind = ir::VocabExpr::Kind::Regex;
        out->strings.push_back(eval_str(node.data.pattern));
        return out;
      } else if constexpr (tag == ast::Tag::Identifier) {
        return resolve_vocab(scope, node.data.name, ctx, loc);
      } else if constexpr (tag == ast::Tag::Parenthesis) {
        return translate_vocab(scope, *node.data.expr, ctx, loc);
      } else if constexpr (tag == ast::Tag::Unary) {
        if (node.data.kind != ast::OpKind::Not) {
          throw CompileError("only `!` (complement) is valid as a unary vocab operator.", loc);
          return nullptr;
        }
        out->kind = ir::VocabExpr::Kind::Complement;
        out->operands.push_back(translate_vocab(scope, *node.data.operand, ctx, loc));
        return out;
      } else if constexpr (tag == ast::Tag::Binary) {
        switch (node.data.kind) {
          case ast::OpKind::BOr:  out->kind = ir::VocabExpr::Kind::Union; break;
          case ast::OpKind::BAnd: out->kind = ir::VocabExpr::Kind::Intersect; break;
          case ast::OpKind::Sub:  out->kind = ir::VocabExpr::Kind::Diff; break;
          default:
            throw CompileError("only `|`, `&`, `-` are valid binary vocab operators.", loc);
            return nullptr;
        }
        out->operands.push_back(translate_vocab(scope, *node.data.lhs, ctx, loc));
        out->operands.push_back(translate_vocab(scope, *node.data.rhs, ctx, loc));
        return out;
      } else {
        throw CompileError("non-vocab expression in a vocab context (expected tokenize/"
                   "regex/vocab reference or a set operation).", loc);
        return nullptr;
      }
    }
  }, expr.data.expr);
}

std::unique_ptr<ir::VocabExpr> Evaluator::resolve_vocab(
  std::string const & scope, std::string const & name, ir::VarMap & ctx,
  std::optional<SourceRange> const & loc
) {
  std::string key = vocab_key(name, ctx);
  auto cit = vocab_cache.find(key);
  if (cit != vocab_cache.end()) return cit->second->canonical();
  if (vocab_in_progress.count(key)) {
    throw CompileError("circular vocab reference involving `" + name + "`.", loc);
    return nullptr;
  }

  // Scope-walk lookup (one level of parent-scope fallback), matching how value
  // symbols are resolved.
  auto sym_it = tables.symbols.find(scope + "::" + name);
  std::string decl_scope = scope;
  if (sym_it == tables.symbols.end()) {
    std::string parent = scope;
    auto sep = parent.rfind("::");
    if (sep != std::string::npos) {
      parent = parent.substr(0, sep);
      sym_it = tables.symbols.find(parent + "::" + name);
      decl_scope = parent;
    }
  }
  if (sym_it == tables.symbols.end()) {
    throw CompileError("undefined vocab `" + name + "`.", loc);
    return nullptr;
  }
  if (!std::holds_alternative<DefineSymbol>(sym_it->second)) {
    throw CompileError("`" + name + "` is not a vocab.", loc);
    return nullptr;
  }
  auto const & defn = std::get<DefineSymbol>(sym_it->second).node;
  if (defn.data.kind != ast::DefineKind::Vocab) {
    throw CompileError("`" + name + "` is not a vocab (used in a vocab expression).", loc);
    return nullptr;
  }
  if (!defn.data.init) {
    throw CompileError("vocab `" + name + "` has no definition.", loc);
    return nullptr;
  }

  vocab_in_progress.insert(key);
  auto ve = translate_vocab(decl_scope, defn.data.init.value(), ctx, loc);
  vocab_in_progress.erase(key);
  if (ve) {
    auto stored = ve->canonical();
    vocab_cache[key] = std::move(ve);
    return stored;
  }
  return nullptr;
}


} // namespace autocog::compiler

