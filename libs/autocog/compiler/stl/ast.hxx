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
  Program,
  Import,
  Export,
  Enum,
  Choice,
  Annotate,
  Annotation,
  Define,
  Flow,
  Edge,
  Struct,
  Field,
  Search,
  Param,
  Retfield,
  Return,
  Record,
  Format,
  Text,
  Prompt,
  FieldRef,
  PromptRef,
  Channel,
  Link,
  Bind,
  Ravel,
  Mapped,
  Prune,
  Wrap,
  Call,
  Kwarg,
  Path,
  Step,
  Expression,
  Identifier,
  Integer,
  Float,
  Boolean,
  String,
  Unary,
  Binary,
  Conditional,
  Parenthesis
};

extern const std::unordered_map<std::string, Tag> tags;

template <Tag tagT>
struct Node;

template <Tag tagT>
struct Data;

template <Tag tagT>
struct BaseExec {
  Node<tagT> & node;
  BaseExec(Node<tagT> & node_) : node(node_) {}
};

template <Tag tagT>
struct Exec : BaseExec<tagT> {
  Exec(Node<tagT> & node) : BaseExec<tagT>(node) {}
};

template <Tag tagT>
struct Node {
  static constexpr Tag tag = tagT;

  Node() : data(), exec(*this) {}

  template<typename... Args>
  Node(Args&&... args) : data{std::forward<Args>(args)...}, exec(*this) {}

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
  Exec<tagT> exec;
};

}

// Recursive expansion helpers
#define EXPAND(...) __VA_ARGS__
#define FOR_EACH_1(what, x) what(x)
#define FOR_EACH_2(what, x, ...) what(x), EXPAND(FOR_EACH_1(what, __VA_ARGS__))
#define FOR_EACH_3(what, x, ...) what(x), EXPAND(FOR_EACH_2(what, __VA_ARGS__))
#define FOR_EACH_4(what, x, ...) what(x), EXPAND(FOR_EACH_3(what, __VA_ARGS__))
#define FOR_EACH_5(what, x, ...) what(x), EXPAND(FOR_EACH_4(what, __VA_ARGS__))
#define FOR_EACH_6(what, x, ...) what(x), EXPAND(FOR_EACH_5(what, __VA_ARGS__))
#define FOR_EACH_7(what, x, ...) what(x), EXPAND(FOR_EACH_6(what, __VA_ARGS__))
#define FOR_EACH_8(what, x, ...) what(x), EXPAND(FOR_EACH_7(what, __VA_ARGS__))
#define FOR_EACH_9(what, x, ...) what(x), EXPAND(FOR_EACH_8(what, __VA_ARGS__))

// Argument counting
#define GET_MACRO(_1,_2,_3,_4,_5,_6,_7,_8,_9,NAME,...) NAME
#define FOR_EACH(action, ...) \
  GET_MACRO(__VA_ARGS__, FOR_EACH_9, FOR_EACH_8, FOR_EACH_7, FOR_EACH_6, FOR_EACH_5, \
            FOR_EACH_4, FOR_EACH_3, FOR_EACH_2, FOR_EACH_1)(action, __VA_ARGS__)

#define NODE(tag) Node< Tag::tag >
#define PNODE(tag) std::unique_ptr< Node< Tag::tag > >
#define ONODE(tag) std::optional< Node< Tag::tag > >
#define NODES(tag) std::list< NODE(tag) >
#define PNODES(tag) std::list< PNODE(tag) >
#define MAPPED(tag) std::unordered_map<std::string, NODE(tag)>
#define VARIANT(...) std::variant<FOR_EACH(NODE, __VA_ARGS__)>
#define VARIANTS(...) std::list< std::variant<FOR_EACH(NODE, __VA_ARGS__)> >

#define DATA(tag) template <> struct Data< Tag::tag >
#define DATA_CTOR(tag) Data< Tag::tag >
#define DATA_CTOR_EMPTY(tag) Data< Tag::tag >()
#define EXEC(tag) template <> struct Exec< Tag::tag > : BaseExec<Tag::tag>
#define EXEC_CTOR(tag) Exec(Node<Tag::tag> & node) : BaseExec<Tag::tag>(node)

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
using Export = NODE(Export);
using Import = NODE(Import);
using Program = NODE(Program);
using FieldRef = NODE(FieldRef);
using PromptRef = NODE(PromptRef);
using Prompt = NODE(Prompt);
using Record = NODE(Record);

}

#undef NODE
#undef PNODE
#undef ONODE
#undef NODES
#undef PNODES
#undef MAPPED
#undef VARIANT
#undef DATA
#undef DATA_CTOR
#undef DATA_CTOR_EMPTY
#undef EXEC
#undef EXEC_CTOR

#endif // AUTOCOG_COMPILER_STL_AST_HXX
