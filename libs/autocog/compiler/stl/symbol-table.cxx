
#include "autocog/compiler/stl/symbol-table.hxx"
#include "autocog/compiler/stl/driver.hxx"
#include "autocog/compiler/stl/evaluate.hxx"

#include <sstream>
#include <stdexcept>

#if VERBOSE
#  include <iostream>
#endif

namespace autocog::compiler::stl {

void SymbolTable::dump_symbols(std::ostream & os) const {
  for (auto const & [name, sym] : symbols) {
    os << "    " << name << " -> ";
    std::visit([&os](auto const & s) {
      using T = std::decay_t<decltype(s)>;
      if constexpr (std::is_same_v<T, DefineSymbol>) {
        os << "Define";
      } else if constexpr (std::is_same_v<T, RecordSymbol>) {
        os << "Record";
      } else if constexpr (std::is_same_v<T, PromptSymbol>) {
        os << "Prompt";
      } else if constexpr (std::is_same_v<T, PythonSymbol>) {
        os << "Python(" << s.filename << ")";
      } else if constexpr (std::is_same_v<T, UnresolvedImport>) {
        os << "UnresolvedImport(fid=" << s.fileid << ")";
      } else if constexpr (std::is_same_v<T, UnresolvedAlias>) {
        os << "UnresolvedAlias(fid=" << s.fileid << ")";
      }
    }, sym);
    os << "\n";
  }
}

void SymbolTable::dump_contexts(std::ostream & os) const {
  for (auto const & [name, context] : contexts) {
    os << "    " << name << ":\n";
    for (auto const & [var, val] : context) {
      os << "      " << var << " = ";
      std::visit([&os](auto const & v) {
        using T = std::decay_t<decltype(v)>;
        if constexpr (std::is_same_v<T, int>) {
          os << v;
        } else if constexpr (std::is_same_v<T, float>) {
          os << v;
        } else if constexpr (std::is_same_v<T, bool>) {
          os << (v ? "true" : "false");
        } else if constexpr (std::is_same_v<T, std::string>) {
          os << "\"" << v << "\"";
        } else if constexpr (std::is_same_v<T, std::nullptr_t>) {
          os << "null";
        }
      }, val);
      os << "\n";
    }
  }
}

}
