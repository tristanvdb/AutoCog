#ifndef AUTOCOG_COMPILER_STL_SYMBOL_TABLE_HXX
#define AUTOCOG_COMPILER_STL_SYMBOL_TABLE_HXX

#include "autocog/compiler/stl/symbols.hxx"
#include "autocog/compiler/stl/ir.hxx"

#include <ostream>

namespace autocog::compiler::stl {

struct SymbolTable {
  std::unordered_map<std::string, AnySymbol> symbols;
  std::unordered_map<std::string, ir::VarMap> contexts;

  void dump_symbols(std::ostream & os) const;
  void dump_contexts(std::ostream & os) const;
};

}

#endif // AUTOCOG_COMPILER_STL_SYMBOL_TABLE_HXX
