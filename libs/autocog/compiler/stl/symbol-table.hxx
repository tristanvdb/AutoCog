#ifndef AUTOCOG_COMPILER_STL_SYMBOL_TABLE_HXX
#define AUTOCOG_COMPILER_STL_SYMBOL_TABLE_HXX

#include "autocog/compiler/stl/symbols.hxx"
#include "autocog/compiler/stl/evaluate.hxx"
#include "autocog/compiler/stl/ir.hxx"

namespace autocog::compiler::stl {

struct SymbolTable {
  std::unordered_map<std::string, AnySymbol> symbols;
  std::unordered_map<std::string, ir::VarMap>  contexts;
  std::unordered_map<std::string, std::string> exports;
};

class Driver;
class SymbolScanner {
  private:
    Driver & driver;

    std::optional<int> fileid;
    std::vector<std::string> scopes;
    bool shortcut_flag = false;

    std::string scope() const;
    
  private:
    // TODO method for `this->driver.tables.symbols.emplace` that check if it already exist.

  public:
    SymbolScanner(Driver & driver);

  public:
    template <ast::Tag tagT>
    bool shortcut(ast::Node<tagT> const &) const {
      return shortcut_flag;
    }

    template <ast::Tag tagT>
    void pre(ast::Node<tagT> const &) {
      shortcut_flag = true;
    }

    template <ast::Tag tagT>
    void post(ast::Node<tagT> const &) {
      shortcut_flag = false;
    }
};

template <>
void SymbolScanner::pre<ast::Tag::Program>(ast::Program const &);

template <>
void SymbolScanner::post<ast::Tag::Program>(ast::Program const &);

template <>
void SymbolScanner::pre<ast::Tag::Import>(ast::Import const &);

template <>
void SymbolScanner::pre<ast::Tag::Alias>(ast::Alias const &);

template <>
void SymbolScanner::pre<ast::Tag::Define>(ast::Define const &);

template <>
void SymbolScanner::pre<ast::Tag::Record>(ast::Record const &);

template <>
void SymbolScanner::post<ast::Tag::Record>(ast::Record const &);

template <>
void SymbolScanner::pre<ast::Tag::Prompt>(ast::Prompt const &);

template <>
void SymbolScanner::post<ast::Tag::Prompt>(ast::Prompt const &);

//class SymbolTablesChecker {
//  private:
//    std::list<Diagnostic> & diagnostics;
//    SymbolTables & symbol_tables;
//
//  public:
//    SymbolTablesChecker(
//      std::list<Diagnostic> & diagnostics_,
//      SymbolTables & symbol_tables_
//    );
//
//  public:
//    template <ast::Tag tagT>
//    void pre(ast::Node<tagT> const &) {
//      // TODO check any reference to prompt, type, or variable
//      // TODO identify node that do that like:
//      //  * Identifier inside Expression
//      //  * PromptRef
//      //  * ...
//    }
//
//    template <ast::Tag tagT>
//    void post(ast::Node<tagT> const &) {}
//};

}

#endif // AUTOCOG_COMPILER_STL_SYMBOL_TABLE_HXX
