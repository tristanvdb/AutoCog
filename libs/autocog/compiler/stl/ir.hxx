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
#include <cstdint>

#include "autocog/runtime/fta/vocab.hxx"
#include "autocog/runtime/sta/path.hxx"
#include "autocog/runtime/sta/channel.hxx"
#include "autocog/runtime/sta/state.hxx"

namespace autocog::compiler::stl::ir {

// A resolved STL scalar value. Identical to the runtime's SearchValue (all STL
// expressions are constexpr, so values are concrete by IR time); shared so the
// IR->STA carry needs no re-wrap. The distinct names mark role (compiler value
// vs search param), not a distinct shape.
using Value = ::autocog::runtime::sta::SearchValue;
using VarMap = std::unordered_map<std::string, Value>;

// Search policies carried from `search { <category>.<param> is <expr>; }`.
// Keyed by category (text/enum/branch/flow/queue), each holding an OPEN map of
// param name -> compile-time-evaluated value. Intentionally not a fixed schema:
// the param set is an experimentation surface, resolved/validated downstream
// (config defaults are merged and concrete values stamped at instantiation).
// A bare `param is value` with no category prefix lands under the "" category.
// Category/scope rules (e.g. flow/queue are global+prompt only) are enforced
// during graph->IR traversal, not at parse time.
using SearchPolicies = std::map<std::string, VarMap>;

// Range for FIELD ARITY only: the repeat count of an array field, like
// items[10] (exactly 10) or hint[0:1] (0 to 1). This is NOT a path selector —
// path-step selection (b[4], c[:4], d[2:5]) uses the shared PathStep below.
// None = scalar field; (n,n) = exactly n; (lo,hi) = lo..hi.
using Range = std::optional<std::pair<int, int>>;

// ---------------------------------------------------------------------------
// Vocab expressions
// ---------------------------------------------------------------------------
// The resolved vocab expression is a shared TA-layer type (runtime/fta/vocab.hxx)
// so the single representation flows IR -> STA -> FTA -> xfta with no parser
// duplication. The IR builds it during translation; str() is used only for
// hashing and STL* unparsing, the tree (vocab_to_json) is the machine carry.
using VocabExpr = ::autocog::runtime::fta::VocabExpr;
using ::autocog::runtime::fta::vocab_hash;
using ::autocog::runtime::fta::vocab_to_json;
using ::autocog::runtime::fta::vocab_from_json;


// Path step (b[4], c[:4], d[2:5], ...) is the shared TA-layer selector type
// (runtime/sta/path.hxx): name + optional index-or-slice selector. Shared so the
// faithful selector flows IR -> STA -> bridge -> Python with no lossy collapse.
using PathStep = ::autocog::runtime::sta::PathStep;
using StepRange = ::autocog::runtime::sta::StepRange;

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

struct Field;

// ---------------------------------------------------------------------------
// Field formats (types). These are plain value structs — no base class, no
// virtuals. A field's format is a std::variant (see `Format` below) of the
// concrete leaf formats plus a field-vector for the container/struct case.
//
// Formats carry ONLY structural/content attributes. The search-tuning
// attributes that used to live here (threshold/beams/ahead/width) are now
// search POLICIES on the Field (Field.search), with the field's own inline
// kwargs folded in as the innermost policy layer during graph->IR.
// ---------------------------------------------------------------------------

// The leaf format types are shared TA-layer types (runtime/sta/state.hxx):
// Completion/Enum/Choice are identical across IR and STA. Only the format
// VARIANT differs (the IR adds a recursive struct case; the STA flattens it to
// monostate), so the variants stay separate but the leaves are shared. The
// Choice path is a plain vector<PathStep> (a choice source is always a local
// field reference, never an input or cross-prompt).
using Completion = ::autocog::runtime::sta::CompletionFormat;
using Enum       = ::autocog::runtime::sta::EnumFormat;
using Choice     = ::autocog::runtime::sta::ChoiceFormat;

// A field's format: one of the shared leaf formats, or a vector of sub-fields
// (the container/struct case). The IR variant keeps the recursive struct case;
// the STA variant (FieldFormat) flattens it to monostate.
using Format = std::variant<
  Completion,
  Enum,
  Choice,
  std::vector<std::unique_ptr<Field>>   // struct: sub-fields
>;

// Field in a prompt or record — i.e. the "self-form record".
struct Field : public Object {
  int depth;                              // Nesting depth (1 for top-level)
  int index;                              // Index within depth level (restarts at 0 per depth)
  Format format;                          // leaf format OR sub-fields (struct)
  Range range;                            // For array fields like "field[lo:hi]"
  SearchPolicies search;                  // Resolved search policy for this field's
                                          // state(s): leaf -> text|enum + branch;
                                          // struct -> branch only.
  std::optional<std::string> refname;     // Original record name (e.g. "Thought"), if any
  VarMap refargs;                         // Record instantiation args (empty if none)

  Field(std::string name_, int depth_, int index_) :
    Object(std::move(name_)),
    depth(depth_),
    index(index_),
    format(),
    range(std::nullopt),
    refargs()
  {}

  bool is_struct() const {
    return std::holds_alternative<std::vector<std::unique_ptr<Field>>>(format);
  }

  // Deep copy
  std::unique_ptr<Field> clone() const {
    auto copy = std::make_unique<Field>(name, depth, index);
    copy->desc = desc;
    copy->range = range;
    copy->search = search;
    copy->refname = refname;
    copy->refargs = refargs;
    std::visit([&](auto const & f) {
      using T = std::decay_t<decltype(f)>;
      if constexpr (std::is_same_v<T, std::vector<std::unique_ptr<Field>>>) {
        std::vector<std::unique_ptr<Field>> fields;
        for (auto const & sub : f) fields.push_back(sub->clone());
        copy->format = std::move(fields);
      } else {
        copy->format = f;
      }
    }, format);
    return copy;
  }
};

// Record: template for field groups
struct Record : public Object {
  std::vector<std::unique_ptr<Field>> fields;
  SearchPolicies search;                  // record-scope search { } params
  std::map<std::string, VocabExpr> vocabs; // vocab table: "vocab_<hash>" -> resolved expr

  Record(std::string name_) :
    Object(std::move(name_)),
    fields()
  {}

  // Deep copy
  std::unique_ptr<Record> clone() const {
    auto copy = std::make_unique<Record>(name);
    copy->desc = desc;
    copy->search = search;
    for (auto const & [k, ve] : vocabs) copy->vocabs.emplace(k, std::move(*ve.canonical()));
    for (auto const& field : fields) {
      copy->fields.push_back(field->clone());
    }
    return copy;
  }
};

// Clause types for data transformation
// Clauses are shared TA-layer types (runtime/sta/channel.hxx); the IR builds
// them and the STA carries them unchanged. RavelClause.depth stays optional
// (nullopt = default 1), resolved at point of use rather than at the boundary.
using BindClause   = ::autocog::runtime::sta::BindClause;
using RavelClause  = ::autocog::runtime::sta::RavelClause;
using WrapClause   = ::autocog::runtime::sta::WrapClause;
using PruneClause  = ::autocog::runtime::sta::PruneClause;
using MappedClause = ::autocog::runtime::sta::MappedClause;
using Clause       = ::autocog::runtime::sta::Clause;

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
  std::vector<Clause> link_clauses;        // Clauses on the link itself (ravel, bind, etc.)

  CallChannel(DocPath target_) :
    target(std::move(target_)),
    extern_func(std::nullopt),
    entry(std::nullopt),
    kwargs(),
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
  SearchPolicies search;                    // prompt-scope search { } params
  std::map<std::string, VocabExpr> vocabs;  // vocab table: "vocab_<hash>" -> resolved expr

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


}

#endif /* AUTOCOG_COMPILER_STL_IR_HXX */
