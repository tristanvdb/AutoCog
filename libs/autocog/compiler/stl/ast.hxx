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

struct SourceRange {
  SourceLocation start;
  SourceLocation stop;
};

enum class AstTag {
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
  Channel,
  Link,
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

template <AstTag tagT>
struct AstNode;

template <AstTag tagT>
struct AstData;

template <AstTag tagT>
struct BaseExec {
  AstNode<tagT> & node;
  BaseExec(AstNode<tagT> & node_) : node(node_) {}

  void semcheck(Parser const & parser, std::string filename, std::list<Diagnostic> & diagnostics) const;
};

template <AstTag tagT>
struct AstExec : BaseExec<tagT> {
  AstExec(AstNode<tagT> & node) : BaseExec<tagT>(node) {}
};

template <AstTag tagT>
struct AstNode {
  static constexpr AstTag tag = tagT;

  AstNode() : data(), exec(*this) {}

  template<typename... Args>
  AstNode(Args&&... args) : data{std::forward<Args>(args)...}, exec(*this) {}

  template<typename... Args>
  static std::unique_ptr<AstNode> make(Args&&... args) {
    return std::make_unique<AstNode>(std::forward<Args>(args)...);
  }

  AstNode(const AstNode&) = delete;
  AstNode& operator=(const AstNode&) = delete;
  AstNode(AstNode&&) = delete;
  AstNode& operator=(AstNode&&) = delete;

  std::optional<SourceRange> location;
  AstData<tagT> data;
  AstExec<tagT> exec;
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

#define NODE(tag) AstNode< AstTag::tag >
#define PNODE(tag) std::unique_ptr< AstNode< AstTag::tag > >
#define ONODE(tag) std::optional< AstNode< AstTag::tag > >
#define NODES(tag) std::list< NODE(tag) >
#define PNODES(tag) std::list< PNODE(tag) >
#define MAPPED(tag) std::unordered_map<std::string, NODE(tag)>
#define VARIANT(...) std::variant<FOR_EACH(NODE, __VA_ARGS__)>

#define DATA(tag) template <> struct AstData< AstTag::tag >
#define DATA_CTOR(tag) AstData< AstTag::tag >
#define DATA_CTOR_EMPTY(tag) AstData< AstTag::tag >()
#define EXEC(tag) template <> struct AstExec< AstTag::tag > : BaseExec<AstTag::tag>
#define EXEC_CTOR(tag) AstExec(AstNode<AstTag::tag> & node) : BaseExec<AstTag::tag>(node)

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

#endif // AUTOCOG_COMPILER_STL_AST_HXX
