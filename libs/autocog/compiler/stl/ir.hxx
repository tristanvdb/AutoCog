#ifndef AUTOCOG_COMPILER_STL_IR_HXX
#define AUTOCOG_COMPILER_STL_IR_HXX

#include <unordered_map>
#include <string>
#include <optional>
#include <variant>
#include <set>
#include <memory>

namespace autocog::compiler::stl::ir {

using Value = std::variant<int, float, bool, std::string, std::nullptr_t>;
using VarMap = std::unordered_map<std::string, Value>;
using DocPath = std::list<std::string>;

struct Object {
  std::string name;
  std::optional<std::string> annotation;

  Object(std::string name_) :
    name(name_)
  {}
};

// Forward declarations
struct Record;
using RecordPtr = std::unique_ptr<Record>;

struct Format : public Object {
  VarMap kwargs;

  Format(std::string name_) :
    Object(std::move(name_))
  {}
};

struct Text : public Format {
  // TODO: vocab source when we support it

  Text(std::string name_) :
    Format(std::move(name_))
  {}
};

struct Enum : public Format {
  std::set<std::string> enumerators;

  Enum(std::string name_) :
    Format(std::move(name_)),
    enumerators()
  {}
};

enum class ChoiceMode { Select, Repeat };

struct Choice : public Format {
  ChoiceMode mode;
  DocPath source;

  Choice(std::string name_, ChoiceMode mode_) :
    Format(std::move(name_)),
    mode(mode_),
    source()
  {}
};

struct Field : public Object {
  std::optional<int> lower;
  std::optional<int> upper;
  std::variant<std::monostate, Text, Enum, Choice, RecordPtr> type;

  Field(std::string name_) :
    Object(std::move(name_)),
    lower(std::nullopt),
    upper(std::nullopt),
    type(std::monostate{})
  {}
};

struct Record : public Object {
  VarMap context;
  std::string mangled;
  std::vector<Field> fields;

  Record(std::string name_, VarMap context_, std::string mangled_) :
    Object(std::move(name_)),
    context(std::move(context_)),
    mangled(std::move(mangled_)),
    fields()
  {}
};

// For Call sources in channels
struct CallInfo {
  std::string entry;

  struct Kwarg {
    std::string name;
    DocPath source;
    bool mapped;
  };
  std::vector<Kwarg> kwargs;

  std::unordered_map<std::string, DocPath> binds;
};

struct Prompt : public Record {

  // Channels (optional)
  struct ChannelLink {
    DocPath target;
    std::variant<std::string, CallInfo> source;
  };
  std::vector<ChannelLink> channels;

  // Flow (optional)
  struct FlowEdge {
    std::string target_prompt;
    std::optional<std::string> label;
  };
  std::vector<FlowEdge> flows;

  // Return (optional)
  struct ReturnInfo {
    std::optional<std::string> label;
    struct Field {
      DocPath source;
      std::optional<std::string> alias;
    };
    std::vector<Field> fields;
  };
  std::optional<ReturnInfo> return_info;

  Prompt(std::string name_, VarMap context_, std::string mangled_) :
    Record(std::move(name_), std::move(context_), std::move(mangled_)),
    channels(), flows(), return_info(std::nullopt)
  {}
};

}

#endif /* AUTOCOG_COMPILER_STL_IR_HXX */
