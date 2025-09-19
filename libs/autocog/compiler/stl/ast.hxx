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
  Program, // 0
  Import,
  Enum,
  Choice,
  Annotate,
  Annotation,
  Define,
  Flow,
  Edge,
  Struct,
  Field, // 10
  Search,
  Param,
  Retfield,
  Return,
  Record,
  Format,
  Text,
  Prompt,
  FieldRef,
  ObjectRef, // 20
  Channel,
  Link,
  Bind,
  Ravel,
  Mapped,
  Prune,
  Wrap,
  Call,
  Kwarg,
  Path, // 30
  Step,
  Expression,
  Identifier,
  Integer,
  Float,
  Boolean,
  String,
  Unary,
  Binary,
  Conditional, // 40
  Parenthesis,
  Assign,
  Alias
};

extern const std::unordered_map<std::string, Tag> tags;

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
    traversal.pre(location, data);
    traverse_children(traversal);
    traversal.post(location, data);
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

using Param = NODE(Param);
using Search = NODE(Search);
using Retfield = NODE(Retfield);
using Return = NODE(Return);
using Enum = NODE(Enum);
using Choice = NODE(Choice);
using Text = NODE(Text);
using Format = NODE(Format);
using Struct = NODE(Struct);
using Field = NODE(Field);
using Annotation = NODE(Annotation);
using Annotate = NODE(Annotate);
using Kwarg = NODE(Kwarg);
using Call = NODE(Call);
using Link = NODE(Link);
using Bind = NODE(Bind);
using Ravel = NODE(Ravel);
using Mapped = NODE(Mapped);
using Prune = NODE(Prune);
using Wrap = NODE(Wrap);
using Channel = NODE(Channel);
using Define = NODE(Define);
using Identifier = NODE(Identifier);
using Integer = NODE(Integer);
using Float = NODE(Float);
using Boolean = NODE(Boolean);
using String = NODE(String);
using Unary = NODE(Unary);
using Binary = NODE(Binary);
using Conditional = NODE(Conditional);
using Parenthesis = NODE(Parenthesis);
using Expression = NODE(Expression);
using Edge = NODE(Edge);
using Flow = NODE(Flow);
using Step = NODE(Step);
using Path = NODE(Path);
using Import = NODE(Import);
using Program = NODE(Program);
using FieldRef = NODE(FieldRef);
using ObjectRef = NODE(ObjectRef);
using Prompt = NODE(Prompt);
using Record = NODE(Record);
using Assign = NODE(Assign);
using Alias = NODE(Alias);

}

#endif // AUTOCOG_COMPILER_STL_AST_HXX
