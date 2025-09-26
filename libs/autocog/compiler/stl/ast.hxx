#ifndef AUTOCOG_COMPILER_STL_AST_HXX
#define AUTOCOG_COMPILER_STL_AST_HXX

#include "autocog/compiler/stl/token.hxx"
#include "autocog/compiler/stl/location.hxx"

#include <string>
#include <list>
#include <queue>
#include <memory>
#include <optional>
#include <variant>
#include <unordered_map>

namespace autocog::compiler::stl {

class Lexer;
class Parser;
struct Diagnostic;

}

namespace autocog::compiler::stl::ast {

enum class Tag {
#define X(etag,stag) etag,
#include "autocog/compiler/stl/ast/nodes.def"
};

extern const std::unordered_map<std::string, Tag> tags;
extern const std::unordered_map<Tag, std::string> rtags;

template <Tag tagT>
struct Data;

template <Tag tagT>
struct Node {
  static constexpr Tag tag = tagT;

  Node() : data() {}

  template<typename... Args>
  Node(Args&&... args) : data{std::forward<Args>(args)...} {}

  template<typename... Args>
  static std::unique_ptr<Node> make(Args&&... args) {
    return std::make_unique<Node>(std::forward<Args>(args)...);
  }

  Node(const Node&) = delete;
  Node & operator=(const Node &) = delete;
  Node(Node &&) = delete;
  Node & operator=(Node &&) = delete;

  std::optional<SourceRange> location;
  Data<tagT> data;

  template <class TraversalT>
  void traverse(TraversalT & traversal) const {
    traversal.pre(*this);
    traverse_children(traversal);
    traversal.post(*this);
  }

  template <class TraversalT>
  void traverse_children(TraversalT & traversal) const;
};

}

#include "autocog/compiler/stl/ast/traits.hxx"

namespace autocog::compiler::stl::ast {

template<class TraversalT, typename T>
std::enable_if_t<is_any_node_container_v<T>> traverse_generic(
  TraversalT & traversal, T const & container
) {
  if constexpr (is_node_v<T>) {
    container.traverse(traversal);
  } else if constexpr (is_pnode_v<T>) {
    if (container) container->traverse(traversal);
  } else if constexpr (is_onode_v<T>) {
    if (container) container->traverse(traversal);
  } else if constexpr (is_nodes_v<T>) {
    for (auto const & node : container)
      node.traverse(traversal);
  } else if constexpr (is_pnodes_v<T>) {
    for (auto const & pnode : container)
      if (pnode)
        pnode->traverse(traversal);
  } else if constexpr (is_variant_v<T>) {
    std::visit([&traversal](auto const & node) {
      if constexpr (is_node_v<decltype(node)>) {
        node.traverse(traversal);
      }
    }, container);
  } else if constexpr (is_variants_v<T>) {
    for (auto const & vnode : container)
      std::visit([&traversal](auto const & node) {
        node.traverse(traversal);
      }, vnode);
  }
}

}

#include "autocog/compiler/stl/ast/macros.hxx"

#include "autocog/compiler/stl/ast/expr.hxx"
#include "autocog/compiler/stl/ast/path.hxx"
#include "autocog/compiler/stl/ast/channel.hxx"
#include "autocog/compiler/stl/ast/return.hxx"
#include "autocog/compiler/stl/ast/search.hxx"
#include "autocog/compiler/stl/ast/struct.hxx"
#include "autocog/compiler/stl/ast/annot.hxx"
#include "autocog/compiler/stl/ast/flow.hxx"
#include "autocog/compiler/stl/ast/define.hxx"
#include "autocog/compiler/stl/ast/prompt.hxx"
#include "autocog/compiler/stl/ast/record.hxx"
#include "autocog/compiler/stl/ast/program.hxx"

namespace autocog::compiler::stl::ast {

#define X(etag,stag) using etag = NODE(etag);
#include "autocog/compiler/stl/ast/nodes.def"

}

#endif // AUTOCOG_COMPILER_STL_AST_HXX
