#ifndef AUTOCOG_PARSER_STL_IR_HXX
#define AUTOCOG_PARSER_STL_IR_HXX

#include "autocog/parser/stl/token.hxx"
#include "autocog/parser/stl/location.hxx"

#include <string>
#include <list>
#include <queue>
#include <memory>
#include <optional>
#include <variant>
#include <unordered_map>

namespace autocog { namespace parser {

class Lexer;
class Parser;
struct Diagnostic;

struct SourceRange {
  SourceLocation start;
  SourceLocation stop;
};

enum class IrTag {
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

template <IrTag tagT>
struct IrNode;

template <IrTag tagT>
struct IrData;

template <IrTag tagT>
struct BaseExec {
  IrNode<tagT> & node;
  BaseExec(IrNode<tagT> & node_) : node(node_) {}

  void semcheck(Parser const & parser, std::string filename, std::list<Diagnostic> & diagnostics) const;
};

template <IrTag tagT>
struct IrExec : BaseExec<tagT> {
  IrExec(IrNode<tagT> & node) : BaseExec<tagT>(node) {}
};

template <IrTag tagT>
struct IrNode {
  static constexpr IrTag tag = tagT;

  IrNode() : data(), exec(*this) {}

  template<typename... Args>
  IrNode(Args&&... args) : data{std::forward<Args>(args)...}, exec(*this) {}

  template<typename... Args>
  static std::unique_ptr<IrNode> make(Args&&... args) {
    return std::make_unique<IrNode>(std::forward<Args>(args)...);
  }

  IrNode(const IrNode&) = delete;
  IrNode& operator=(const IrNode&) = delete;
  IrNode(IrNode&&) = delete;
  IrNode& operator=(IrNode&&) = delete;

  std::optional<SourceRange> location;
  IrData<tagT> data;
  IrExec<tagT> exec;
};

} }

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

#define NODE(tag) IrNode< IrTag::tag >
#define PNODE(tag) std::unique_ptr< IrNode< IrTag::tag > >
#define ONODE(tag) std::optional< IrNode< IrTag::tag > >
#define NODES(tag) std::list< NODE(tag) >
#define PNODES(tag) std::list< PNODE(tag) >
#define MAPPED(tag) std::unordered_map<std::string, NODE(tag)>
#define VARIANT(...) std::variant<FOR_EACH(NODE, __VA_ARGS__)>

#define DATA(tag) template <> struct IrData< IrTag::tag >
#define DATA_CTOR(tag) IrData< IrTag::tag >
#define DATA_CTOR_EMPTY(tag) IrData< IrTag::tag >()
#define EXEC(tag) template <> struct IrExec< IrTag::tag > : BaseExec<IrTag::tag>
#define EXEC_CTOR(tag) IrExec(IrNode<IrTag::tag> & node) : BaseExec<IrTag::tag>(node)

#include "autocog/parser/stl/ir/expr.hxx"
#include "autocog/parser/stl/ir/path.hxx"
#include "autocog/parser/stl/ir/channel.hxx"
#include "autocog/parser/stl/ir/return.hxx"
#include "autocog/parser/stl/ir/search.hxx"
#include "autocog/parser/stl/ir/struct.hxx"
#include "autocog/parser/stl/ir/annot.hxx"
#include "autocog/parser/stl/ir/flow.hxx"
#include "autocog/parser/stl/ir/define.hxx"
#include "autocog/parser/stl/ir/prompt.hxx"
#include "autocog/parser/stl/ir/record.hxx"
#include "autocog/parser/stl/ir/program.hxx"

#endif // AUTOCOG_PARSER_STL_IR_HXX
