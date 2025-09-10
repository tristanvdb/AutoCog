
#include "instantiate.hxx"

#include <stdexcept>

#if VERBOSE
#  include <iostream>
#endif

namespace autocog::compiler::stl {

CompileError::CompileError(
  std::string msg,
  std::optional<SourceRange> loc
) :
  message(std::move(msg)),
  location(loc)
{}
  
const char * CompileError::what() const noexcept {
  return message.c_str();
}

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

Instantiator::Instantiator(
  std::unordered_map<std::string, ast::Program> const & programs_,
  std::list<Diagnostic> & diagnostics_
) :
  programs(programs_),
  diagnostics(diagnostics_),
  globals(),
  symbols()
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

#define DEBUG_Instantiator_evaluate_defines VERBOSE && 0

void Instantiator::evaluate_defines() {
#if DEBUG_Instantiator_evaluate_defines
    std::cerr << "Instantiator::evaluate_defines" << std::endl;
#endif
  for (auto const & [filename, program]: programs) {
#if DEBUG_Instantiator_evaluate_defines
    std::cerr << "    " << filename << std::endl;
#endif
    if (filename != program.data.filename) {
      throw std::runtime_error("Inconsistency of Program's filename.");
    }
    auto & varmap = globals[filename];
    for (auto const & [varname, defn]: program.data.defines) {
      if (defn.data.name != varname) {
        throw std::runtime_error("Inconsistency of Define statement name.");
      }

      if (defn.data.argument) {
        emit_error("Top level definition must be arguments!", defn.location);
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

void Instantiator::scan_import_statement(ast::Program const & program, ast::Import const & import) {
  auto & local_symbols = symbols[program.data.filename];
  auto const & filename = import.data.file;
  bool has_stl_ext = filename.size() >= 4 && ( filename.rfind(".stl") == filename.size() - 4 );
  bool has_py_ext = filename.size() >= 3 && ( filename.rfind(".py") == filename.size() - 3 );
  if (has_stl_ext) {
    auto prog_it = programs.find(filename);
    if (prog_it == programs.end()) {
      throw std::runtime_error("STL file `" + filename + "` in import statement was not parsed.");
    }
    auto const & imported_program = prog_it->second;
    for (auto [alias,target]: import.data.targets) {
      auto record_it = imported_program.data.records.find(target);
      auto prompt_it = imported_program.data.prompts.find(target);

      if (record_it != imported_program.data.records.end()) {
        local_symbols.emplace(alias, RecordSymbol(imported_program, record_it->second, alias));

      } else if (prompt_it != imported_program.data.prompts.end()) {
        local_symbols.emplace(alias, PromptSymbol(imported_program, prompt_it->second, alias));

      } else {
        local_symbols.emplace(alias, UnresolvedImport(imported_program, import, alias));

      }
    }
  } else if (has_py_ext) {
    for (auto const & [alias,target]: import.data.targets) {
      local_symbols.emplace(alias, PythonSymbol(filename, target, alias));
    }
  } else {
    emit_error("Imported file with unknown extension.", import.location);
  }
}

#define DEBUG_Instantiator_generate_symbols VERBOSE && 0

void Instantiator::generate_symbols() {
#if DEBUG_Instantiator_generate_symbols
    std::cerr << "Instantiator::generate_symbols" << std::endl;
#endif
  for (auto const & [filename, program]: programs) {
    for (auto const & import_statement: program.data.imports) {
      scan_import_statement(program, import_statement);
    }
  }
  bool resolved_symbols = false;
  do {
    // TODO resolve any `UnresolvedImport`
  } while (resolved_symbols);
}

#define DEBUG_Instantiator_instantiate VERBOSE && 0

void Instantiator::instantiate() {
#if DEBUG_Instantiator_instantiate
    std::cerr << "Instantiator::instantiate" << std::endl;
#endif
//  for (auto const & [filename, program]: programs) {
//    for (auto const & [name, exported]: program.data.exports) {
//      // TODO ???
//    }
//  }
}

} // namespace autocog::compiler

