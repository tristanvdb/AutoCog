
#include "autocog/compiler/stl/driver.hxx"

#include "autocog/compiler/stl/parser.hxx"
#include "autocog/compiler/stl/symbol-scanner.hxx"
#include "autocog/compiler/stl/evaluate.hxx"
#include "autocog/compiler/stl/instance-scanner.hxx"
//#include "autocog/compiler/stl/instantiate.hxx"

#include "autocog/compiler/stl/ast/tostring.hxx"
#include "autocog/compiler/stl/ast/printer.hxx"

#include <filesystem>
#include <tuple>
#include <stdexcept>

#if VERBOSE
#  include <iostream>
#endif

namespace autocog::compiler::stl {

void Driver::emit_error(std::string msg, std::optional<SourceRange> const & loc) {
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

bool Driver::report_errors() {
  for (auto const & diag : diagnostics) {
    std::cerr << diag.format(fileids) << std::endl;
    switch (diag.level) {
      case DiagnosticLevel::Error:   errors++;   break;
      case DiagnosticLevel::Warning: warnings++; break;
      case DiagnosticLevel::Note:    notes++;    break;
    }
  }

  if (errors > 0) {
    std::cerr << "Failed with " << errors << " error(s), " << warnings << " warning(s), and " << notes << " note(s).\n";
  } else if (warnings > 0) {
    std::cerr << "Passed with " << warnings << " warning(s) and " << notes << " note(s).\n";
  } else if (notes > 0) {
    std::cerr << "Passed with " << notes << " note(s).\n";
  }
  diagnostics.clear();

  return errors > 0;
}

std::optional<int> Driver::fileid(std::string const & filename) const {
  auto it = this->fileids.find(filename);
  if (it != this->fileids.end()) {
    return it->second;
  }
  return std::nullopt;
}

static std::tuple<int, std::optional<std::string>, std::string> parse_scope(std::string const & scope) {
  auto first_delim = scope.find("::");
  if (first_delim == std::string::npos) {
    throw std::runtime_error("Invalid scope format: " + scope);
  }
  
  int fileid;
  try {
    fileid = std::stoi(scope.substr(0, first_delim));
  } catch (...) {
    throw std::runtime_error("Invalid fileid in scope: " + scope);
  }
  
  auto rest = scope.substr(first_delim + 2);
  auto second_delim = rest.find("::");
  
  if (second_delim == std::string::npos) {
    if (rest.empty()) {
      throw std::runtime_error("Empty name in scope: " + scope);
    }
    return {fileid, std::nullopt, rest};
  } else {
    auto object = rest.substr(0, second_delim);
    auto name = rest.substr(second_delim + 2);
    if (object.empty() || name.empty()) {
      throw std::runtime_error("Empty object or name in scope: " + scope);
    }
    return {fileid, object, name};
  }
}

//static std::string format_scope(int fid, std::optional<std::string> const & scope) {
//  return scope ? std::to_string(fid) + "::" + scope.value() : std::to_string(fid);
//}

std::optional<int> Driver::compile__() {
  // 1 - Parse all files

  Parser parser(this->diagnostics, this->fileids, this->includes, this->programs, this->inputs);
  parser.parse();
  if (this->report_errors()) return 101;

#if !defined(NDEBUG)
  std::cerr << "After parsing (#1):" << std::endl;
  for (auto const & program: programs) {
    std::cerr << "  " << program.data.fid << ": " << program.data.filename << std::endl;
    ast::printTagTree(program, std::cerr, "> ");
  }
#endif

  // 2 - Collect symbols
  SymbolScanner symbol_scanner(*this);
  this->traverse_ast(symbol_scanner);
  if (this->report_errors()) return 102;

#if !defined(NDEBUG)
  std::cerr << "After collecting symbol (#2):" << std::endl;
  this->tables.dump(std::cerr);
#endif

  // Evaluate global defines

  std::vector<std::tuple<int, std::string, std::optional<SourceRange>>> need_evaluation;
  for (auto const & [qname, symbol]: this->tables.symbols) {
    if (std::holds_alternative<DefineSymbol>(symbol)) {
      auto const & defn = std::get<DefineSymbol>(symbol);
      auto [fid, obj, name] = parse_scope(qname);
      if (!obj) {
        auto def_it = this->defines.find(name);
        if (def_it != this->defines.end()) {
          auto sfid = std::to_string(fid);
          auto & context = this->tables.contexts[sfid];
          context[name] = def_it->second;
        } else {
          need_evaluation.emplace_back(fid,name,defn.node.location);
        }
        
      }
    }
  }
  Evaluator evaluator(this->diagnostics, this->tables);
  for (auto [fid,name,loc]: need_evaluation) {
    auto sfid = std::to_string(fid);
    auto & context = this->tables.contexts[sfid];
    try {
      evaluator.retrieve_value(sfid, name, context, loc); // FIXME could avoid a lookup by calling `evaluate` directly...
    } catch (CompileError const & e) {
      emit_error(e.message, e.location);
    }
  }
  if (this->report_errors()) return 103;

#if !defined(NDEBUG)
  std::cerr << "After evaluating globals (#3):" << std::endl;
  this->tables.dump(std::cerr);
#endif

  // Collect instantiations

  InstanceScanner instance_scanner(*this);
  this->traverse_ast(instance_scanner);
  for ([[maybe_unused]] auto & [objptr, path]: instance_scanner.objects) {
#if !defined(NDEBUG)
    std::cerr << "> object: " << ast::refString(*objptr) << std::endl;
#endif
    // TODO

  }
  for ([[maybe_unused]] auto & [fmtptr, path]: instance_scanner.formats) {
#if !defined(NDEBUG)
    std::cerr << "> format: " << ast::refString(*fmtptr) << std::endl;
#endif
    // TODO
  }
  if (this->report_errors()) return 104;

#if !defined(NDEBUG)
  std::cerr << "After collecting instantiations (#4):" << std::endl;
  this->tables.dump(std::cerr);
#endif

  // Instantiate all exported prompts associated to input files

//  Instantiator instantiator(programs, diagnostics, symbol_tables);
//
//  instantiator.evaluate_defines();
//  if (report_errors()) return 3;
//
//  instantiator.generate_symbols();
//  if (report_errors()) return 4;
//
//  SymbolTablesChecker symbol_tables_checker(diagnostics, symbol_tables);
//  traverse_ast(symbol_tables_checker);
//  if (report_errors()) return 5;
//
//  instantiator.instantiate();
//  if (report_errors()) return 6;

  return std::nullopt;
}

std::optional<int> Driver::compile() {
  return autocog::utilities::wrap_exception(&Driver::compile__, *this);
}

int Driver::backend() {
  // TODO
  return 0;
}

}
