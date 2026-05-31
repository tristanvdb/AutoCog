#ifndef AUTOCOG_COMPILER_STL_IR_HXX
#define AUTOCOG_COMPILER_STL_IR_HXX

#include <unordered_map>
#include <map>
#include <string>
#include <optional>
#include <variant>
#include <vector>
#include <list>
#include <memory>

namespace autocog::compiler::stl::ir {

using Value = std::variant<int, float, bool, std::string, std::nullptr_t>;
using VarMap = std::unordered_map<std::string, Value>;

// Search parameters carried from `search { <category>.<param> is <expr>; }`.
// Keyed by category (text/enum/branch/flow/queue), each holding an OPEN map of
// param name -> compile-time-evaluated value. Intentionally not a fixed schema:
// the param set is an experimentation surface, resolved/validated downstream
// (config defaults are merged and concrete values stamped at instantiation).
// A bare `param is value` with no category prefix lands under the "" category.
// Category/scope rules (e.g. flow/queue are global+prompt only) are enforced
// during graph->IR traversal, not at parse time.
using SearchParams = std::map<std::string, VarMap>;

// Range for array indexing: [start:end] or [index]
// None means no range, (n,n) means [n], (start,end) means [start:end]
using Range = std::optional<std::pair<int, int>>;

// Path step: name with optional range like "field[3:6]"
struct PathStep {
  std::string name;
  Range range;

  PathStep(std::string name_, Range range_ = std::nullopt) :
    name(std::move(name_)),
    range(range_)
  {}
};

// Document path: represents paths like "?input", "other.field[2]", ".result"
struct DocPath {
  std::vector<PathStep> steps;
  bool is_input;                           // True for external input (?field)
  std::optional<std::string> prompt;       // For cross-prompt dataflow (other.field)

  DocPath() :
    steps(),
    is_input(false),
    prompt(std::nullopt)
  {}

  DocPath(std::vector<PathStep> steps_, bool is_input_ = false, std::optional<std::string> prompt_ = std::nullopt) :
    steps(std::move(steps_)),
    is_input(is_input_),
    prompt(std::move(prompt_))
  {}
};

// Base class for all named IR objects
struct Object {
  std::string name;
  std::vector<std::string> desc;  // Annotations for LLM prompt engineering

  Object(std::string name_) :
    name(std::move(name_)),
    desc()
  {}
};

// Forward declarations
struct Field;
struct Prompt;
struct Record;
using RecordPtr = std::unique_ptr<Record>;

// Base class for field formats (types)
struct Format : public Object {
  std::optional<std::string> refname;  // For format aliases/references

  Format(std::string name_) :
    Object(std::move(name_)),
    refname(std::nullopt)
  {}

  virtual ~Format() = default;
  virtual std::unique_ptr<Format> clone() const = 0;  // Deep copy
};

// Text/Completion format: text<length=N, threshold=X, ...>
struct Completion : public Format {
  std::optional<int> length;
  std::optional<float> threshold;
  std::optional<int> beams;
  std::optional<int> ahead;
  std::optional<int> width;
  std::optional<std::vector<std::string>> within;

  Completion(std::string name_) :
    Format(std::move(name_)),
    length(std::nullopt),
    threshold(std::nullopt),
    beams(std::nullopt),
    ahead(std::nullopt),
    width(std::nullopt),
    within(std::nullopt)
  {}

  std::unique_ptr<Format> clone() const override {
    auto copy = std::make_unique<Completion>(name);
    copy->desc = desc;
    copy->refname = refname;
    copy->length = length;
    copy->threshold = threshold;
    copy->beams = beams;
    copy->ahead = ahead;
    copy->width = width;
    copy->within = within;
    return copy;
  }
};

// Enum format: enum("A", "B", "C")
struct Enum : public Format {
  std::vector<std::string> values;
  std::optional<int> width;

  Enum(std::string name_) :
    Format(std::move(name_)),
    values(),
    width(std::nullopt)
  {}

  std::unique_ptr<Format> clone() const override {
    auto copy = std::make_unique<Enum>(name);
    copy->desc = desc;
    copy->refname = refname;
    copy->values = values;
    copy->width = width;
    return copy;
  }
};

// Choice format: select(source) or repeat(source)
struct Choice : public Format {
  DocPath path;
  std::string mode;  // "select" or "repeat"
  std::optional<int> width;

  Choice(std::string name_, std::string mode_) :
    Format(std::move(name_)),
    path(),
    mode(std::move(mode_)),
    width(std::nullopt)
  {}

  std::unique_ptr<Format> clone() const override {
    auto copy = std::make_unique<Choice>(name, mode);
    copy->desc = desc;
    copy->refname = refname;
    copy->path = path;
    copy->width = width;
    return copy;
  }
};

// Forward declaration for RecordFormat
struct Field;

// Record format: embeds a full record structure as a field type
struct RecordFormat : public Format {
  std::vector<std::unique_ptr<Field>> fields;  // Embedded record fields

  RecordFormat(std::string name_) :
    Format(std::move(name_)),
    fields()
  {}

  std::unique_ptr<Format> clone() const override;  // Defined after Field
};

// Field in a prompt or record
struct Field : public Object {
  int depth;                              // Nesting depth (1 for top-level)
  int index;                              // Index within depth level (restarts at 0 per depth)
  std::optional<std::unique_ptr<Format>> format;  // Owned format or embedded record
  Range range;                            // For array fields like "field[10]"

  Field(std::string name_, int depth_, int index_) :
    Object(std::move(name_)),
    depth(depth_),
    index(index_),
    format(std::nullopt),
    range(std::nullopt)
  {}

  // Deep copy
  std::unique_ptr<Field> clone() const {
    auto copy = std::make_unique<Field>(name, depth, index);
    copy->desc = desc;
    copy->range = range;
    if (format.has_value()) {
      copy->format = (*format)->clone();
    }
    return copy;
  }
};

// Record: template for field groups
struct Record : public Object {
  std::vector<std::unique_ptr<Field>> fields;
  SearchParams search;                    // record-scope search { } params

  Record(std::string name_) :
    Object(std::move(name_)),
    fields()
  {}

  // Deep copy
  std::unique_ptr<Record> clone() const {
    auto copy = std::make_unique<Record>(name);
    copy->desc = desc;
    copy->search = search;
    for (auto const& field : fields) {
      copy->fields.push_back(field->clone());
    }
    return copy;
  }
};

// Clause types for data transformation
struct BindClause {
  std::vector<PathStep> source;           // Source path (empty = implicit)
  std::vector<PathStep> target;           // Target path (empty = implicit)
};

struct RavelClause {
  std::optional<int> depth;               // Flatten depth (nullopt = 1)
  std::vector<PathStep> target;           // Optional target path
};

struct WrapClause {
  std::vector<PathStep> target;           // Optional target path
};

struct PruneClause {
  std::vector<PathStep> target;           // Path to prune
};

struct MappedClause {
  std::vector<PathStep> target;           // Optional target path
};

using Clause = std::variant<BindClause, RavelClause, WrapClause, PruneClause, MappedClause>;

// Kwarg for function/prompt calls
struct Kwarg {
  bool is_input;                          // True if source is external input
  std::optional<std::string> prompt;      // For cross-prompt dataflow
  std::vector<PathStep> path;             // Source path
  std::optional<std::string> value;       // Literal value (from "is" keyword)
  std::vector<Clause> clauses;            // Transformation clauses

  Kwarg() :
    is_input(false),
    prompt(std::nullopt),
    path(),
    value(std::nullopt),
    clauses()
  {}
};

// Channel types (discriminated union)
struct InputChannel {
  DocPath target;
  std::vector<PathStep> source;  // External input path

  InputChannel(DocPath target_, std::vector<PathStep> source_) :
    target(std::move(target_)),
    source(std::move(source_))
  {}
};

struct DataflowChannel {
  DocPath target;
  std::optional<std::string> prompt;  // Source prompt (nullopt = current prompt)
  std::vector<PathStep> source;       // Source field path
  std::vector<Clause> clauses;        // Transformation clauses

  DataflowChannel(DocPath target_, std::optional<std::string> prompt_, std::vector<PathStep> source_) :
    target(std::move(target_)),
    prompt(std::move(prompt_)),
    source(std::move(source_)),
    clauses()
  {}
};

struct CallChannel {
  DocPath target;
  std::optional<std::string> extern_func;  // "module.function" for Python calls
  std::optional<std::string> entry;        // For prompt calls
  std::unordered_map<std::string, Kwarg> kwargs;
  std::optional<std::unordered_map<std::string, std::vector<PathStep>>> binds;
  std::vector<Clause> link_clauses;        // Clauses on the link itself (ravel, bind, etc.)

  CallChannel(DocPath target_) :
    target(std::move(target_)),
    extern_func(std::nullopt),
    entry(std::nullopt),
    kwargs(),
    binds(std::nullopt),
    link_clauses()
  {}
};

using Channel = std::variant<InputChannel, DataflowChannel, CallChannel>;

// Flow edge: control flow to another prompt
struct FlowEdge {
  std::string target_prompt;              // Mangled name of target prompt
  std::optional<std::string> label;       // Optional label for the flow
  std::optional<int> limit;               // Optional limit for loops (e.g., [5])

  FlowEdge(std::string target_) :
    target_prompt(std::move(target_)),
    label(std::nullopt),
    limit(std::nullopt)
  {}
};

// Return statement
struct ReturnField {
  DocPath source;                         // Which field to return
  std::optional<std::string> alias;       // Optional alias for the field
  std::optional<std::string> constant;    // Compile-time constant value (from Expression source)
  std::vector<Clause> clauses;            // Transformation clauses

  ReturnField(DocPath source_) :
    source(std::move(source_)),
    alias(std::nullopt),
    constant(std::nullopt),
    clauses()
  {}
};

struct ReturnInfo {
  std::optional<std::string> label;       // Optional label for return edge
  std::vector<ReturnField> fields;

  ReturnInfo() :
    label(std::nullopt),
    fields()
  {}
};

// Prompt: executable unit with fields, channels, flows
struct Prompt : public Object {
  std::string mangled_name;               // Unique mangled name with parameters
  VarMap context;                         // Parameter values for this instantiation
  std::vector<std::unique_ptr<Field>> fields;
  std::vector<Channel> channels;
  std::vector<FlowEdge> flows;
  std::optional<ReturnInfo> return_info;
  SearchParams search;                    // prompt-scope search { } params

  Prompt(std::string name_, std::string mangled_name_, VarMap context_) :
    Object(std::move(name_)),
    mangled_name(std::move(mangled_name_)),
    context(std::move(context_)),
    fields(),
    channels(),
    flows(),
    return_info(std::nullopt)
  {}
};

// Program: top-level IR container
struct Program {
  std::optional<std::string> desc;
  std::unordered_map<std::string, std::string> entries;  // entry name -> mangled name
  std::unordered_map<std::string, std::unique_ptr<Prompt>> prompts;
  std::unordered_map<std::string, std::unique_ptr<Format>> formats;
  std::unordered_map<std::string, std::unique_ptr<Record>> records;

  Program() :
    desc(std::nullopt),
    entries(),
    prompts(),
    formats(),
    records()
  {}
};

// Implementation of RecordFormat::clone() - must be after Field definition
inline std::unique_ptr<Format> RecordFormat::clone() const {
  auto copy = std::make_unique<RecordFormat>(name);
  copy->desc = desc;
  copy->refname = refname;
  for (auto const& field : fields) {
    copy->fields.push_back(field->clone());
  }
  return copy;
}

}

#endif /* AUTOCOG_COMPILER_STL_IR_HXX */
