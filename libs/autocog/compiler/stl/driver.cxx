
#include "autocog/compiler/stl/driver.hxx"

#include "autocog/compiler/stl/parser.hxx"
#include "autocog/compiler/stl/symbol-table.hxx"
#include "autocog/compiler/stl/instantiate.hxx"

#include <filesystem>

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

std::optional<int> Driver::compile() {
  Parser parser(this->diagnostics, this->fileids, this->includes, this->programs, this->inputs);

  // Parse all files

  parser.parse();
  if (this->report_errors()) return 2;

  // Collect symbols

  SymbolScanner scanner(*this);
  this->traverse_ast(scanner);
  if (this->report_errors()) return 3;

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

int Driver::backend() {
  // TODO
  return 0;
}

}
