#ifndef AUTOCOG_COMPILER_STL_SYMBOLS_HXX
#define AUTOCOG_COMPILER_STL_SYMBOLS_HXX

#include "autocog/compiler/stl/ast.hxx"

namespace autocog::compiler::stl {

template <class AstNode>
struct AstSymbol {
  AstNode const & node;
  std::string alias;

  AstSymbol(
    AstNode const & node_,
    std::string const & alias_
  );
};

using DefineSymbol = AstSymbol<ast::Define>;
using RecordSymbol = AstSymbol<ast::Record>;
using PromptSymbol = AstSymbol<ast::Prompt>;

struct PythonSymbol {
  std::string filename;
  std::string alias;
  ast::ObjectRef const & target;
  
  PythonSymbol(
    std::string const & filename_,
    std::string const & alias_,
    ast::ObjectRef const & target_
  );
};

template <class AstNode>
struct UnresolvedSymbol {
  int fileid;
  std::string alias;
  ast::ObjectRef const & target;
  AstNode const & node;
  
  UnresolvedSymbol(
    int fileid_,
    std::string const & alias_,
    ast::ObjectRef const & target_,
    AstNode const & node_
  );
};

using UnresolvedImport = UnresolvedSymbol<ast::Import>;
using UnresolvedAlias = UnresolvedSymbol<ast::Alias>;

using AnySymbol = std::variant<DefineSymbol, RecordSymbol, PromptSymbol, PythonSymbol, UnresolvedImport, UnresolvedAlias>;

}

#include "autocog/compiler/stl/symbols.txx"

#endif // AUTOCOG_COMPILER_STL_SYMBOLS_HXX
