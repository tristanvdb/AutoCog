#ifndef AUTOCOG_COMPILER_STL_SYMBOL_TABLE_HXX
#define AUTOCOG_COMPILER_STL_SYMBOL_TABLE_HXX

#include "autocog/compiler/stl/ast.hxx"

namespace autocog::compiler::stl {

class SymbolTables {
  public:
    // TODO
};

class SymbolTablesBuilder {
  private:
    std::list<Diagnostic> & diagnostics;
    SymbolTables & symbol_tables;
    std::optional<std::string> current_file;

  public:
    SymbolTablesBuilder(
      std::list<Diagnostic> & diagnostics_,
      SymbolTables & symbol_tables_
    );

  public:
    template <ast::Tag tagT>
    void pre(std::optional<SourceRange> const &, ast::Data<tagT> const &) {}

    template <ast::Tag tagT>
    void post(std::optional<SourceRange> const &, ast::Data<tagT> const &) {}
};

template <>
void SymbolTablesBuilder::pre<ast::Tag::Program>(std::optional<SourceRange> const &, ast::Data<ast::Tag::Program> const &);

template <>
void SymbolTablesBuilder::post<ast::Tag::Program>(std::optional<SourceRange> const &, ast::Data<ast::Tag::Program> const &);

class SymbolTablesChecker {
  private:
    std::list<Diagnostic> & diagnostics;
    SymbolTables & symbol_tables;

  public:
    SymbolTablesChecker(
      std::list<Diagnostic> & diagnostics_,
      SymbolTables & symbol_tables_
    );

  public:
    template <ast::Tag tagT>
    void pre(std::optional<SourceRange> const &, ast::Data<tagT> const &) {
      // TODO check any reference to prompt, type, or variable
      // TODO identify node that do that like:
      //  * Identifier inside Expression
      //  * PromptRef
      //  * ...
    }

    template <ast::Tag tagT>
    void post(std::optional<SourceRange> const &, ast::Data<tagT> const &) {}
};

}

#endif // AUTOCOG_COMPILER_STL_SYMBOL_TABLE_HXX
