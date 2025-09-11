#ifndef AUTOCOG_COMPILER_STL_IR_HXX
#define AUTOCOG_COMPILER_STL_IR_HXX

#include <unordered_map>
#include <string>
#include <optional>
#include <variant>
#include <memory>

namespace autocog::compiler::stl::ir {

using Value = std::variant<int, float, bool, std::string, std::nullptr_t>;
using VarMap = std::unordered_map<std::string, ir::Value>;

struct Object {
  std::string name;
  std::optional<std::string> annotation;

  Object(
    std::string & name_
  ) :
    name(name_),
    annotation(std::nullopt)
  {}
};

struct Format : public Object {
  VarMap kwargs;

  // TODO native kind? Maybe have Text, Enum, Choice instead of just Format

  Format(
    std::string name_
  ) :
    Object(name_),
    kwargs()
  {}
};

struct Record;
using RecordPtr = std::unique_ptr<Record>;

struct Field : public Object {
  std::optional<int> lower;
  std::optional<int> upper;
  std::variant<std::monostate, Format, RecordPtr> type;

  Field(
    std::string name_
  ) :
    Object(name_),
    lower(std::nullopt),
    upper(std::nullopt),
    type(std::monostate{})
  {}
};

struct Record : public Object {
  VarMap context;
  std::string mangled;

  // TODO

  Record(
    std::string name_,
    VarMap context_,
    std::string mangled_
  ) :
    Object(name_),
    context(context_),
    mangled(mangled_)
  {}
};

struct Prompt : public Object {
  VarMap context;
  std::string mangled;

  // TODO

  Prompt(
    std::string name_,
    VarMap context_,
    std::string mangled_
  ) :
    Object(name_),
    context(context_),
    mangled(mangled_)
  {}
};

}

#endif /* AUTOCOG_COMPILER_STL_IR_HXX */
