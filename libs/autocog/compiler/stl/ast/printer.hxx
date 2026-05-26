#ifndef AUTOCOG_COMPILER_STL_AST_PRINTER_HXX
#define AUTOCOG_COMPILER_STL_AST_PRINTER_HXX

#include "autocog/compiler/stl/ast.hxx"

namespace autocog::compiler::stl::ast {

struct TagTreeTraversal {
  std::ostream & out;
  std::string prefix;
  std::string indent;
  int depth;
    
  TagTreeTraversal(std::ostream & out_, std::string prefix_="", std::string indent_="  ") 
      : out(out_), prefix(prefix_), indent(indent_), depth(0) {}
    
  template<Tag T>
  void pre(Node<T> const &) {
    out << prefix;
    for (int i = 0; i < depth; ++i) out << indent;
    out << tag2str(T) << "\n";
    ++depth;
  }
    
  template<Tag T>
  void post(Node<T> const &) {
    --depth;
  }
    
  template<Tag T>
  bool shortcut(Node<T> const &) { 
    return false;
  }
};

template<Tag T>
void printTagTree(Node<T> const & node, std::ostream & out = std::cout, std::string prefix = "", std::string indent = "  ") {
  TagTreeTraversal traversal(out, prefix, indent);
  node.traverse(traversal);
}

}

#endif /* AUTOCOG_COMPILER_STL_AST_PRINTER_HXX */
