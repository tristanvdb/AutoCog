
#include "autocog/compiler/stl/symbol-table.hxx"

#include <stdexcept>

#if VERBOSE
#  include <iostream>
#endif

namespace autocog::compiler::stl {

SymbolTablesBuilder::SymbolTablesBuilder(
  std::list<Diagnostic> & diagnostics_,
  SymbolTables & symbol_tables_
) :
  diagnostics(diagnostics_),
  symbol_tables(symbol_tables_)
{}


template <>
void SymbolTablesBuilder::pre<ast::Tag::Program>(std::optional<SourceRange> const &, ast::Data<ast::Tag::Program> const & data) {
  if (current_file) throw std::runtime_error("Should not try to overwrite SymbolTablesBuilder::current_file");
  current_file = data.filename;
}

template <>
void SymbolTablesBuilder::post<ast::Tag::Program>(std::optional<SourceRange> const &, ast::Data<ast::Tag::Program> const &) {
  if (!current_file) throw std::runtime_error("Should not try to erase empty SymbolTablesBuilder::current_file");
  current_file = std::nullopt;
}

SymbolTablesChecker::SymbolTablesChecker(
  std::list<Diagnostic> & diagnostics_,
  SymbolTables & symbol_tables_
) :
  diagnostics(diagnostics_),
  symbol_tables(symbol_tables_)
{}


}
