#ifndef AUTOCOG_COMPILER_STL_SYMBOL_SCANNER_HXX
#define AUTOCOG_COMPILER_STL_SYMBOL_SCANNER_HXX

#include "autocog/compiler/stl/symbol-table.hxx"

namespace autocog::compiler::stl {

class Driver;

class SymbolScanner {
  private:
    Driver & driver;

    std::optional<int> fileid;
    std::vector<std::string> scopes;
    bool shortcut_flag;

    std::string scope() const;

  private:
    // TODO method for `this->driver.tables.symbols.emplace` that check if it already exist.

  public:
    SymbolScanner(Driver & driver_);

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

}

#endif // AUTOCOG_COMPILER_STL_SYMBOL_SCANNER_HXX
