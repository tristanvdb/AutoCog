
#include "instantiate.hxx"

#include <stdexcept>

#if VERBOSE
#  include <iostream>
#endif

namespace autocog::compiler::stl {

void Instantiator::emit_error(std::string msg, std::optional<SourceRange> const & loc) {
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

ir::Value Instantiator::assign(std::string const & filename, std::string const & varname, ast::Expression const & expr, ir::VarMap & env) {
  auto vit = env.find(varname);
  if (vit == env.end()) {
    ir::Value res = evaluate(filename, expr, env);
    env[varname] = res;
    return res;
  } else {
    return vit->second;
  }
}

#define DEBUG_Instantiator_defines VERBOSE && 0

void Instantiator::defines(ast::Program const & program) {
  // FIXME this implementation depends on the order `define` decl are seem. It won't work as we use an unordered map...

  auto & env = globals[program.data.filename];
  for (auto const & [varname, defn]: program.data.defines) {
#if DEBUG_Instantiator_defines
    std::cerr << "Defining: " << varname << std::endl;
#endif
    if (defn.data.argument) {
#if DEBUG_Instantiator_defines
      std::cerr << "  Is an argument!" << std::endl;
#endif
      emit_error("Top level definition must be arguments!", defn.location);
      continue;
    }
    if (!defn.data.init) {
#if DEBUG_Instantiator_defines
      std::cerr << "  No initializer!" << std::endl;
#endif
      emit_error("Define without initializer!", defn.location);
      continue;
    }

    assign(program.data.filename, varname, defn.data.init.value(), env);
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

