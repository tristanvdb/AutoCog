
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

std::optional<int> Driver::compile() {
  Parser parser(diagnostics, fileids, includes, programs, inputs);

  // Parse all files

  parser.parse();
  if (report_errors()) return 2;

  // Semantic analysis

  SymbolTablesBuilder symbol_tables_builder(diagnostics, symbol_tables);
  traverse_ast(symbol_tables_builder);
  SymbolTablesChecker symbol_tables_checker(diagnostics, symbol_tables);
  traverse_ast(symbol_tables_checker);

  // Instantiate all exported prompts associated to input files

//  Instantiator instantiator(parser.get(), diagnostics);
//
//  instantiator.evaluate_defines();
//  if (report_errors()) return 3;
//
//  instantiator.generate_symbols();
//  if (report_errors()) return 4;
//
//  instantiator.instantiate();
//  if (report_errors()) return 5;

  return std::nullopt;
}

int Driver::backend() {
  // TODO
  return 0;
}

}
