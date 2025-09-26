
#include "autocog/compiler/stl/symbol-table.hxx"
#include "autocog/compiler/stl/driver.hxx"

#include <sstream>
#include <stdexcept>

#if VERBOSE
#  include <iostream>
#endif

namespace autocog::compiler::stl {

void SymbolTable::dump(std::ostream & os) const {
  os << "   === Symbols ===\n";
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
  
  os << "  === Contexts ===\n";
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
  
//  os << "  === Exports ===\n";
//  for (auto const & [from, to] : exports) {
//    os << "    " << from << " -> " << to << "\n";
//  }
}

SymbolScanner::SymbolScanner(
  Driver & driver_
) :
  driver(driver_),
  scopes()
{}

static std::string join(const std::vector<std::string>& vec, const std::string& sep) {
  if (vec.empty()) return "";
    
  std::ostringstream oss;
  auto it = vec.begin();
  oss << *it++;
  for (; it != vec.end(); ++it) {
    oss << sep << *it;
  }
  return oss.str();
}

std::string SymbolScanner::scope() const {
  return join(this->scopes, "::");
}

template <>
void SymbolScanner::pre<ast::Tag::Program>(ast::Program const & node) {
  this->fileid = node.data.fid;
  this->scopes.push_back(std::to_string(node.data.fid));
}

template <>
void SymbolScanner::post<ast::Tag::Program>(ast::Program const &) {
  this->scopes.pop_back();
  this->fileid = std::nullopt;
}

template <>
void SymbolScanner::pre<ast::Tag::Import>(ast::Import const & node) {
  this->shortcut_flag = true;
  auto scope = this->scope();
  auto const & filename = node.data.file;
  bool has_stl_ext = filename.size() >= 4 && ( filename.rfind(".stl") == filename.size() - 4 );
  bool has_py_ext = filename.size() >= 3 && ( filename.rfind(".py") == filename.size() - 3 );
  if (has_stl_ext) {
    auto fid = this->driver.fileid(filename);
    if (!fid) {
      throw std::runtime_error("STL file should have been parsed!!!");
    }
    for (auto const & alias_node: node.data.targets) {
      auto const & target = alias_node.data.target;
      auto alias = scope + "::";
      if (alias_node.data.alias) {
        alias += alias_node.data.alias.value().data.name;
      } else {
        if (target.data.config.size() > 0) {
          this->driver.emit_error("Imported parametrized objects must be given a local alias (\"as\" keyword).", alias_node.location);
          continue;
        }
        alias += target.data.name.data.name;
      }
      auto sym = UnresolvedImport(fid.value(), alias, target, node);
      this->driver.tables.symbols.emplace(alias, sym);
    }
  } else if (has_py_ext) {
    for (auto const & alias_node: node.data.targets) {
      auto const & target = alias_node.data.target;
      auto alias = scope + "::";
      if (alias_node.data.alias) {
        alias += alias_node.data.alias.value().data.name;
      } else {
        if (target.data.config.size() > 0) {
          this->driver.emit_error("Imported parametrized objects must be given a local alias (\"as\" keyword).", alias_node.location);
          continue;
        }
        alias += target.data.name.data.name;
      }
      auto sym = PythonSymbol(filename, alias, target);
      this->driver.tables.symbols.emplace(alias, sym);
    }
  } else {
    this->driver.emit_error("Imported file with unknown extension.", node.location);
  }
}

template <>
void SymbolScanner::pre<ast::Tag::Alias>(ast::Alias const & node) {
  this->shortcut_flag = true;
  auto alias = this->scope() + "::";
  if (node.data.alias) {
    alias += node.data.alias.value().data.name;
  } else {
    if (!node.data.is_export) {
      this->driver.emit_error("Alias without specifying an alias name.", node.location);
      return;
    } else if (node.data.target.data.config.size() > 0) {
      this->driver.emit_error("Export parametrized object without specifying an alias name.", node.location);
      return;
    }
    alias += node.data.target.data.name.data.name;
  }
  auto sym = UnresolvedAlias(this->fileid.value(), alias, node.data.target, node);
  this->driver.tables.symbols.emplace(alias, sym);
}

template <>
void SymbolScanner::pre<ast::Tag::Define>(ast::Define const & node) {
  this->shortcut_flag = true;
  auto alias = this->scope() + "::" + node.data.name.data.name;
  auto sym = DefineSymbol(node, alias);
  this->driver.tables.symbols.emplace(alias, sym);
}

template <>
void SymbolScanner::pre<ast::Tag::Record>(ast::Record const & node) {
  this->scopes.push_back(node.data.name.data.name);
  auto alias = this->scope();
  RecordSymbol sym(node, alias);
  this->driver.tables.symbols.emplace(alias, sym);
}

template <>
void SymbolScanner::post<ast::Tag::Record>(ast::Record const &) {
  this->scopes.pop_back();
}

template <>
void SymbolScanner::pre<ast::Tag::Prompt>(ast::Prompt const & node) {
  this->scopes.push_back(node.data.name.data.name);
  auto alias = this->scope();
  PromptSymbol sym(node, alias);
  this->driver.tables.symbols.emplace(alias, sym);
}

template <>
void SymbolScanner::post<ast::Tag::Prompt>(ast::Prompt const &) {
  this->scopes.pop_back();
}

//SymbolTablesChecker::SymbolTablesChecker(
//  std::list<Diagnostic> & diagnostics_,
//  SymbolTables & symbol_tables_
//) :
//  diagnostics(diagnostics_),
//  symbol_tables(symbol_tables_)
//{}


}
