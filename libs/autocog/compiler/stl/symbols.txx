
namespace autocog::compiler::stl {

template <class AstNode>
AstSymbol<AstNode>::AstSymbol(
  AstNode const & node_,
  std::string const & alias_
) :
  node(node_),
  alias(alias_)
{}

template <class AstNode>
UnresolvedSymbol<AstNode>::UnresolvedSymbol(
  int fileid_,
  std::string const & alias_,
  ast::ObjectRef const & target_,
  AstNode const & node_
) :
  fileid(fileid_),
  alias(alias_),
  target(target_),
  node(node_)
{}

}

