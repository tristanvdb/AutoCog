
#include "instantiate.hxx"

#include <stdexcept>

#if VERBOSE
#  include <iostream>
#endif

namespace autocog::compiler::stl {

#define DEBUG_Instantiator_emit_error VERBOSE && 1

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

Instantiator::Instantiator(std::list<Diagnostic> & diagnostics_) :
  diagnostics(diagnostics_),
  globals(),
  declaration_to_file(),
  records(),
  prompts(),
  instances(),
  record_cache(),
  instantiations()
{}

ir::Value Instantiator::assign(ast::Program const & program, std::string const & varname, ast::Expression const & expr, ir::VarMap & env) {
  auto vit = env.find(varname);
  if (vit == env.end()) {
    ir::Value res = evaluate(program, expr, env);
    env[varname] = res;
    return res;
  } else {
    return vit->second;
  }
}

static std::string valueToString(ir::Value const & value) {
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

#define DEBUG_Instantiator_defines VERBOSE && 1

void Instantiator::define_one(ast::Program const & program, ast::Define const & defn, ir::VarMap & varmap) {
#if DEBUG_Instantiator_defines
  std::cerr << "Instantiator::define_one(defn=" << defn.data.name << ")" << std::endl;
#endif

  if (defn.data.argument) {
    emit_error("Top level definition must be arguments!", defn.location);
    return;
  }

  if (!defn.data.init) {
    emit_error("Define without initializer!", defn.location);
    return;
  }

  if (varmap.find(defn.data.name) == varmap.end()) {
    assign(program, defn.data.name, defn.data.init.value(), varmap);
  }
}

bool Instantiator::define_one(ast::Program const & program, std::string const & varname, ir::VarMap & varmap) {
#if DEBUG_Instantiator_defines
  std::cerr << "Instantiator::define_one(string=" << varname << ")" << std::endl;
#endif
  auto it = program.data.defines.find(varname);
  if (it == program.data.defines.end()) {
    return false;
  } else {
    define_one(program, it->second, varmap);
    return true;
  }
}

void Instantiator::defines(ast::Program const & program) {
#if DEBUG_Instantiator_defines
    std::cerr << "Instantiator::defines" << std::endl;
#endif
  auto & varmap = globals[program.data.filename];
  for (auto const & [varname, defn]: program.data.defines) {
    if (defn.data.name != varname) {
      throw std::runtime_error("Inconsistency of Define statement name.");
    }
    define_one(program, defn, varmap);
#if DEBUG_Instantiator_defines
    std::cerr << "> varmap[" << defn.data.name << "] = " << valueToString(varmap[defn.data.name]) << std::endl;
#endif
  }
}

void Instantiator::declarations(ast::Program const & program) {
  throw std::runtime_error("NIY: Instantiator::declarations");
}

void Instantiator::entries(ast::Program const & program) {
  throw std::runtime_error("NIY: Instantiator::entries");
}

void Instantiator::collect() {
  throw std::runtime_error("NIY: Instantiator::collect");
}

void Instantiator::instantiate() {
  throw std::runtime_error("NIY: Instantiator::instantiate");
}

} // namespace autocog::compiler

