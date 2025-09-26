
#include "instantiate.hxx"

#include <sstream>
#include <cmath>
#include <algorithm>
#include <stdexcept>

#if VERBOSE
#  include <iostream>
#endif

namespace autocog::compiler::stl {

#define DEBUG_Instantiator_emit_error VERBOSE && 0

void Instantiator::emit_error(std::string msg, std::optional<SourceRange> const & loc) {
#if DEBUG_Instantiator_emit_error
  std::cerr << "Instantiator::emit_error" << std::endl;
#endif
  if (loc) {
    auto start = loc.value().start;
    diagnostics.emplace_back(DiagnosticLevel::Error, msg, start);
  } else {
    diagnostics.emplace_back(DiagnosticLevel::Error, msg);
  }
}

Instantiator::Instantiator(
  std::list<, ast::Program> const & programs_,
  std::list<Diagnostic> & diagnostics_,
  SymbolTables & tables_
) :
  programs(programs_),
  diagnostics(diagnostics_),
  tables(tables_),
  exports(),
  instantiations(),
  record_cache()
{}

[[maybe_unused]] static std::string valueToString(ir::Value const & value) {
  return std::visit([](auto const & v) -> std::string {
    using V = std::decay_t<decltype(v)>;
    if constexpr (std::is_same_v<V, int>) {
      return "int(" + std::to_string(v) + ")";
    } else if constexpr (std::is_same_v<V, float>) {
      std::string s = std::to_string(v);
      // Remove trailing zeros after decimal point
      if (s.find('.') != std::string::npos) {
        s.erase(s.find_last_not_of('0') + 1, std::string::npos);
        if (s.back() == '.') {
          s.pop_back();
        }
      }
      return "float(" + s + ")";
    } else if constexpr (std::is_same_v<V, bool>) {
      return "bool(" + std::string(v ? "true" : "false") + ")";
    } else if constexpr (std::is_same_v<V, std::string>) {
      return "string(\"" + v + "\")";
    } else {
      return "unknown()";
    }
  }, value);
}

// FNV-1a 64-bit hash for deterministic cross-platform hashing
uint64_t fnv1a_hash(const std::string& str) {
  uint64_t hash = 0xcbf29ce484222325ULL; // FNV-1a 64-bit offset basis
  for (unsigned char c : str) {
    hash ^= static_cast<uint64_t>(c);    // XOR with byte
    hash *= 0x100000001b3ULL;            // Multiply by FNV-1a 64-bit prime
  }
  return hash;
}

std::string mangle(std::string const & name, ir::VarMap const & varmap) {
  if (varmap.empty()) {
    return name;
  }
  
  std::string mangled = name;
  
  // Sort parameters by name for deterministic mangling
  std::vector<std::pair<std::string, ir::Value>> sorted_params(varmap.begin(), varmap.end());
  std::sort(sorted_params.begin(), sorted_params.end());
  
  for (auto const & [param_name, value] : sorted_params) {
    mangled += "__" + param_name + "_";
    
    std::visit([&mangled](auto const & v) {
      using V = std::decay_t<decltype(v)>;
      if constexpr (std::is_same_v<V, int>) {
        mangled += "i" + std::to_string(v);
      } else if constexpr (std::is_same_v<V, float>) {
        // Handle special float values
        if (std::isnan(v)) {
          mangled += "fNaN";
        } else if (std::isinf(v)) {
          mangled += v > 0 ? "fInf" : "fNegInf";
        } else {
          // Use hex float representation for exact value preservation
          std::ostringstream oss;
          oss << std::hexfloat << v;
          std::string hex_str = oss.str();
          // Replace problematic characters
          std::replace(hex_str.begin(), hex_str.end(), '.', 'd');
          std::replace(hex_str.begin(), hex_str.end(), '+', 'p');
          std::replace(hex_str.begin(), hex_str.end(), '-', 'm');
          mangled += "f" + hex_str;
        }
      } else if constexpr (std::is_same_v<V, bool>) {
        mangled += v ? "bT" : "bF";
      } else if constexpr (std::is_same_v<V, std::string>) {
        // Use FNV-1a hash for string values
        uint64_t hash_value = fnv1a_hash(v);
        std::ostringstream oss;
        oss << "s" << std::hex << hash_value;
        mangled += oss.str();
      } else if constexpr (std::is_same_v<V, std::nullptr_t>) {
        mangled += "null";
      }
    }, value);
  }
  
  return mangled;
}

#define DEBUG_Instantiator_evaluate_defines VERBOSE && 0

void Instantiator::evaluate_defines() {
#if DEBUG_Instantiator_evaluate_defines
    std::cerr << "Instantiator::evaluate_defines" << std::endl;
#endif
  for (auto const & program: programs) {
#if DEBUG_Instantiator_evaluate_defines
    std::cerr << "    " << program.data.filename << std::endl;
#endif
    auto & varmap = tables.globals[program.data.filename];
    for (auto const & [varname, defn]: program.data.defines) {
      if (defn.data.name != varname) {
        throw std::runtime_error("Inconsistency of Define statement name.");
      }

      if (defn.data.argument) {
        emit_error("Top level definition must not be arguments!", defn.location);
        return;
      }

      if (!defn.data.init) {
        emit_error("Define without initializer!", defn.location);
        return;
      }

      try {
        retrieve_value(program, defn.data.name, varmap, defn.location);
      } catch (CompileError const & e) {
        emit_error(e.message, e.location);
      }
#if DEBUG_Instantiator_evaluate_defines
      std::cerr << "> varmap[" << defn.data.name << "] = " << valueToString(varmap[defn.data.name]) << std::endl;
#endif
    }
  }
}

void Instantiator::scan_import_statement(SymbolTable & symtbl, ast::Import const & import) {
  auto const & filename = import.data.file;
  bool has_stl_ext = filename.size() >= 4 && ( filename.rfind(".stl") == filename.size() - 4 );
  bool has_py_ext = filename.size() >= 3 && ( filename.rfind(".py") == filename.size() - 3 );
  if (has_stl_ext) {
    auto prog_it = programs.begin();
    while (prog_it != programs.end()) {
      if (program.data.filename == filename) break;
      ++prog_it;
    }
    if (prog_it == programs.end()) {
      throw std::runtime_error("STL file `" + filename + "` in import statement was not parsed.");
    }
    for (auto [alias,target]: import.data.targets) {
      symtbl.emplace(alias, UnresolvedImport(filename, target, import));
    }
  } else if (has_py_ext) {
    for (auto const & [alias,target]: import.data.targets) {
      symtbl.emplace(alias, PythonSymbol(filename, target, alias));
    }
  } else {
    emit_error("Imported file with unknown extension.", import.location);
  }
}

static void replace_symbol(SymbolTable & symtbl, std::string const & alias, AnySymbol const & source) {
  symtbl.erase(alias);
  std::visit([&symtbl, &alias](auto const & sym) {
    using T = std::decay_t<decltype(sym)>;
    if constexpr (std::is_same_v<T, RecordSymbol> || std::is_same_v<T, PromptSymbol>) {
      symtbl.emplace(alias, T(sym.scope, sym.node, sym.name));
    } else if constexpr (std::is_same_v<T, PythonSymbol>) {
      symtbl.emplace(alias, T(sym.filename, sym.callable, sym.name));
    } else {
      throw std::runtime_error("Helper function `replace_symbol` should never have been called with an `UnresolvedImport`.");
    }
  }, source);
}

#define DEBUG_Instantiator_generate_symbols VERBOSE && 0

void Instantiator::generate_symbols() {
#if DEBUG_Instantiator_generate_symbols
    std::cerr << "Instantiator::generate_symbols" << std::endl;
#endif
  for (auto const & program: programs) {
    SymbolTable & symtbl = tables.symbols[program.data.filename];
    for (auto const & import_statement: program.data.imports) {
      scan_import_statement(symtbl, import_statement);
    }
    for (auto const & [name, record]: program.data.records) {
      symtbl.emplace(name, RecordSymbol(program, record, name));
    }
    for (auto const & [name, prompt]: program.data.prompts) {
      symtbl.emplace(name, PromptSymbol(program, prompt, name));
    }
  }
  bool resolved_symbols = true;
  while (resolved_symbols) {
    resolved_symbols = false;
    for (auto & [fname, symtbl]: tables.symbols) {
#if DEBUG_Instantiator_generate_symbols
      std::cerr << "  IN " << fname << std::endl;
#endif
      std::vector<std::pair<std::string, UnresolvedImport>> unresolved_symbols;
      for (auto & [alias, symbol]: symtbl) {
#if DEBUG_Instantiator_generate_symbols
        std::cerr << "    SEE " << alias << std::endl;
#endif
        if (std::holds_alternative<UnresolvedImport>(symbol)) {
          auto & unresolved = std::get<UnresolvedImport>(symbol);
#if DEBUG_Instantiator_generate_symbols
          std::cerr << "      unresolved " << unresolved.objname << " from " << unresolved.filename << std::endl;
#endif
          unresolved_symbols.emplace_back(alias, unresolved);
        }
      }
#if DEBUG_Instantiator_generate_symbols
      std::cerr << "    FOUND " << unresolved_symbols.size() << " unresolved symbols" << std::endl;
#endif
      for (auto & [alias, unresolved]: unresolved_symbols) {
        auto & imported_symtbl = tables.symbols[unresolved.filename];
        auto sym_it = imported_symtbl.find(unresolved.objname);
        if (sym_it == imported_symtbl.end()) {
          emit_error("Trying to import a non-existant object `" + unresolved.objname + "`", unresolved.import.location);
          symtbl.erase(alias);
        } else if (!std::holds_alternative<UnresolvedImport>(sym_it->second)) {
#if DEBUG_Instantiator_generate_symbols
          std::cerr << "      resolving " << alias << " from " << fname << std::endl;
#endif
          replace_symbol(symtbl, alias, sym_it->second);
          resolved_symbols = true;
        }
      }
    }
  }
  
  for (auto & [fname, symtbl]: tables.symbols) {
    for (auto & [alias, symbol]: symtbl) {
      if (std::holds_alternative<UnresolvedImport>(symbol)) {
        auto & unresolved = std::get<UnresolvedImport>(symbol);
        emit_error("Could not resolve `" + unresolved.objname + "` (likely a circular dependency).", unresolved.import.location);
      }
    }
  }
}

#define DEBUG_Instantiator_scoped_context VERBOSE && 0

template <class ScopeT>
ir::VarMap Instantiator::scoped_context(
  ScopeT const & scope,
  Kwargs const & kwargs,
  ir::VarMap const & parent_context,
  std::optional<SourceRange> const & loc
) {
#if DEBUG_Instantiator_scoped_context
  std::cerr << "Instantiator::scoped_context" << std::endl;
#endif

  // Step 1: Validate all kwargs correspond to actual arguments
  for (auto const& [name, expr] : kwargs) {
#if DEBUG_Instantiator_scoped_context
    std::cerr << "  kwargs: " << name << std::endl;
#endif
    auto def_it = scope.data.defines.find(name);
    if (def_it == scope.data.defines.end()) {
      throw CompileError("Unknown parameter: " + name, loc);
    }
    auto const & defn = def_it->second;
#if DEBUG_Instantiator_scoped_context
    std::cerr << "  defn: " << defn.data.name << std::endl;
    std::cerr << "    arg:  " << defn.data.argument << std::endl;
    std::cerr << "    init: " << (defn.data.init?"present":"") << std::endl;
#endif
    if (!defn.data.argument) {
      throw CompileError("Non-argument parameter: " + name, defn.location);
    }
  }
  
  // Step 2: Evaluate kwargs in parent context ONLY
  ir::VarMap evaluated_kwargs;
  ir::VarMap parent_copy = parent_context;  // for const correctness
  for (auto const& [name, expr] : kwargs) {
    evaluated_kwargs[name] = evaluate(scope, expr, parent_copy);
  }
  
  // Step 3: Build object varmap
  ir::VarMap object_context = parent_context;
  
  // Step 4: Remove shadowed variables (all defines shadow parent scope)
  for (auto const& [name, define] : scope.data.defines) {
    object_context.erase(name);
  }
  
  // Step 5: Insert evaluated kwargs into object varmap
  for (auto const& [name, value] : evaluated_kwargs) {
    object_context[name] = value;
  }
  
  // Step 6: Validate all arguments have values (kwargs or init)
  for (auto const& [name, define] : scope.data.defines) {
    if (define.data.argument) {
      if (kwargs.find(name) == kwargs.end() && !define.data.init) {
        throw CompileError("Missing required argument: " + name);
      }
    }
  }
  
  // Step 7: Force evaluation of all defines (both arguments and locals)
  for (auto const& [name, define] : scope.data.defines) {
    retrieve_value(scope, name, object_context);
  }
  
  return object_context;
}

template <class ObjectT>
std::string mangle(
  ObjectT const & object,
  ir::VarMap const & context__
) {
  ir::VarMap context;
  for (auto const& [name, define] : object.data.defines) {
    if (define.data.argument) {
      context[name] = context__.at(name);
    }
  }
  return mangle(object.data.name, context);
}

#define DEBUG_Instantiator_instantiate VERBOSE && 1

template <>
std::string Instantiator::instantiate<ast::Record>(
  ast::Record const & record, 
  Kwargs const & kwargs,
  ir::VarMap const & context__,
  std::optional<SourceRange> const & loc
) {
  ir::VarMap context = scoped_context(record, kwargs, context__, loc);
  std::string mangled_name = mangle(record.data.name, context);

  if (record_cache.find(mangled_name) == record_cache.end()) {
    record_cache.emplace(mangled_name, ir::Record{record.data.name, context, mangled_name});
    // TODO
  }

  return mangled_name;
}

template <>
std::string Instantiator::instantiate<ast::Prompt>(
  ast::Prompt const & prompt, 
  Kwargs const & kwargs,
  ir::VarMap const & context__,
  std::optional<SourceRange> const & loc
) {
  ir::VarMap context = scoped_context(prompt, kwargs, context__, loc);
  std::string mangled_name = mangle(prompt.data.name, context);

  if (instantiations.find(mangled_name) == instantiations.end()) {
    instantiations.emplace(mangled_name, ir::Prompt{prompt.data.name, context, mangled_name});
    // TODO explore this prompt's channels and flow to find references for more instantiations 
  }

  return mangled_name;
}

void Instantiator::instantiate() {
#if DEBUG_Instantiator_instantiate
  std::cerr << "Instantiator::instantiate" << std::endl;
#endif
  for (auto const & program: programs) {
    SymbolTable const & symtbl = tables.symbols[program.data.filename];
#if DEBUG_Instantiator_instantiate
    std::cerr << "  IN " << program.data.filename << std::endl;
#endif
    for (auto const & exported: program.data.exports) {
#if DEBUG_Instantiator_instantiate
      std::cerr << "    EXPORT " << exported.data.alias << " from " << exported.data.target << std::endl;
#endif
      auto target_it = symtbl.find(exported.data.target);
      if (target_it == symtbl.end()) {
        emit_error("Trying to export something that does not exist.", exported.location);
        continue;
      } else if (!std::holds_alternative<PromptSymbol>(target_it->second)) {
        emit_error("Only prompt can be exported.", exported.location);
        continue;
      }

      auto const & symbol = std::get<PromptSymbol>(target_it->second);
      try {
        std::string mangled_name = instantiate(symbol.node, exported.data.kwargs, tables.globals[symbol.scope.data.filename]);
        tables.exports.emplace(exported.data.alias, mangled_name);
      } catch (CompileError const & e) {
        emit_error(e.message, e.location);
      }
    }
  }
}

} // namespace autocog::compiler

