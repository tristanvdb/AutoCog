
#include "autocog/compiler/stl/symbol-scanner.hxx"
#include "autocog/utilities/exception.hxx"
#include "autocog/compiler/stl/driver.hxx"

#include <sstream>
#include <stdexcept>


namespace autocog::compiler::stl {

SymbolScanner::SymbolScanner(
  Driver & driver_
) :
  driver(driver_),
  fileid(std::nullopt),
  scopes(),
  shortcut_flag(false)
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
  auto scope = std::to_string(node.data.fid);
  this->scopes.push_back(scope);
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
      throw autocog::utilities::InternalError("STL file should have been parsed!!!");
    }
    for (auto const & alias_node: node.data.targets) {
      auto const & target = alias_node.data.target;
      std::string name;
      if (alias_node.data.alias) {
        name = alias_node.data.alias.value().data.name;
      } else {
        if (target.data.config.size() > 0) {
          this->driver.emit_error("Imported parametrized objects must be given a local alias (\"as\" keyword).", alias_node.location);
          continue;
        }
        name = target.data.name.data.name;
      }
      auto alias = scope + "::" + name;
      auto sym = UnresolvedImport(fid.value(), alias, target, node);

      if (!this->driver.tables.symbols.emplace(alias, sym).second) {
        this->driver.emit_error("Already have a object with name " + name + " in scope " + scope + ".", alias_node.location);
        return;
      }
    }
  } else if (has_py_ext) {
    for (auto const & alias_node: node.data.targets) {
      auto const & target = alias_node.data.target;
      std::string name;
      if (alias_node.data.alias) {
        name = alias_node.data.alias.value().data.name;
      } else {
        if (target.data.config.size() > 0) {
          this->driver.emit_error("Imported parametrized objects must be given a local alias (\"as\" keyword).", alias_node.location);
          continue;
        }
        name = target.data.name.data.name;
      }
      auto alias = scope + "::" + name;
      auto sym = PythonSymbol(filename, alias, target);

      if (!this->driver.tables.symbols.emplace(alias, sym).second) {
        this->driver.emit_error("Already have a object with name " + name + " in scope " + scope + ".", alias_node.location);
        return;
      }
    }
  } else {
    this->driver.emit_error("Imported file with unknown extension.", node.location);
  }
}

template <>
void SymbolScanner::pre<ast::Tag::Alias>(ast::Alias const & node) {
  this->shortcut_flag = true;
  std::string name;
  if (node.data.alias) {
    name = node.data.alias.value().data.name;
  } else {
    if (!node.data.is_export) {
      this->driver.emit_error("Alias without specifying an alias name.", node.location);
      return;
    } else if (node.data.target.data.config.size() > 0) {
      this->driver.emit_error("Export parametrized object without specifying an alias name.", node.location);
      return;
    }
    name = node.data.target.data.name.data.name;
  }
  auto scope = this->scope();
  auto alias = scope + "::" + name;
  auto sym = UnresolvedAlias(this->fileid.value(), alias, node.data.target, node);

  if (!this->driver.tables.symbols.emplace(alias, sym).second) {
    this->driver.emit_error("Already have a object with name " + name + " in scope " + scope + ".", node.location);
    return;
  }
}

template <>
void SymbolScanner::pre<ast::Tag::Define>(ast::Define const & node) {
  this->shortcut_flag = true;
  auto name = node.data.name.data.name;
  auto scope = this->scope();
  auto alias = scope + "::" + name;
  auto sym = DefineSymbol(node, alias);

  if (!this->driver.tables.symbols.emplace(alias, sym).second) {
    this->driver.emit_error("Already have a object with name " + name + " in scope " + scope + ".", node.data.name.location);
    return;
  }
}

template <>
void SymbolScanner::pre<ast::Tag::Record>(ast::Record const & node) {
  auto scope = this->scope();
  auto name = node.data.name.data.name;
  this->scopes.push_back(name);
  auto alias = this->scope();
  RecordSymbol sym(node, alias);
  if (!this->driver.tables.symbols.emplace(alias, sym).second) {
    this->driver.emit_error("Already have a object with name " + name + " in scope " + scope + ".", node.data.name.location);
    return;
  }
}

template <>
void SymbolScanner::post<ast::Tag::Record>(ast::Record const &) {
  this->scopes.pop_back();
}

template <>
void SymbolScanner::pre<ast::Tag::Prompt>(ast::Prompt const & node) {
  auto scope = this->scope();
  auto name = node.data.name.data.name;
  this->scopes.push_back(name);
  auto alias = this->scope();
  PromptSymbol sym(node, alias);
  if (!this->driver.tables.symbols.emplace(alias, sym).second) {
    this->driver.emit_error("Already have a object with name " + name + " in scope " + scope + ".", node.data.name.location);
    return;
  }
}

template <>
void SymbolScanner::post<ast::Tag::Prompt>(ast::Prompt const &) {
  this->scopes.pop_back();
}

}
