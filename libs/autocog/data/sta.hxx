#ifndef AUTOCOG_DATA_STA_HXX
#define AUTOCOG_DATA_STA_HXX

#include "autocog/data/base.hxx"
#include "autocog/data/path.hxx"
#include "autocog/data/channel.hxx"
#include "autocog/data/vocab.hxx"
#include "autocog/utilities/types.hxx"

#include <map>
#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace autocog::data {

// Conversion for every type below lives in the free functions in
// json.hxx / python.hxx; these carry identity (hash) only.

// --- SearchParams: open dict category -> key -> scalar -----------------------

struct SearchParams {
  // category -> key -> resolved scalar (monostate = null).
  std::map<std::string, std::map<std::string, autocog::types::Value>> categories;

  bool empty() const { return categories.empty(); }

  std::string hash() const;
};

// --- Field format ------------------------------------------------------------

struct CompletionFormat { std::optional<int> length; std::optional<std::string> vocab; };
struct EnumFormat       { std::vector<std::string> values; };
struct ChoiceFormat     { std::string mode; std::vector<PathStep> path; };

struct FieldFormat {
  std::variant<std::monostate, CompletionFormat, EnumFormat, ChoiceFormat> value;  // monostate = record

  std::string hash() const;
};

// --- Flow --------------------------------------------------------------------

struct ReturnField { std::string alias; std::vector<PathStep> path; };
struct FlowControl { std::string prompt; std::optional<int> limit; };
struct FlowReturn  { std::vector<ReturnField> fields; };

struct Flow {
  std::variant<FlowControl, FlowReturn> value;

  std::string hash() const;
};

// --- Abstract state ----------------------------------------------------------

/// One node of the abstract state graph; field/flow/exit are indices (-1 = none).
struct Abstract { int field = -1; int flow = -1; int exit_ = -1; };

// --- Field -------------------------------------------------------------------

struct Field {
  std::string name;
  int depth = 0;
  int index = 0;
  int flat_index = 0;
  std::vector<std::string> desc;
  std::optional<std::pair<int,int>> range;   // null = scalar
  FieldFormat format;
  std::optional<std::string> format_ref;
  std::vector<std::string> format_desc;
  SearchParams search;

  std::string hash() const;

  // Structural queries used by the runtime (pure functions of the members).
  bool is_list() const { return range.has_value(); }
  bool is_record() const { return std::holds_alternative<std::monostate>(format.value); }
  std::string tag() const {
    return name + "_" + std::to_string(depth) + "_" + std::to_string(flat_index);
  }
};

// --- Prompt ------------------------------------------------------------------

struct Prompt {
  std::string name;
  std::vector<std::string> desc;
  std::vector<Field> fields;
  std::vector<Abstract> abstracts;
  std::map<std::string, Flow> flows;
  std::vector<Channel> channels;
  SearchParams search;
  std::map<std::string, VocabExpr> vocabs;

  std::string hash() const;
};

// --- Schema & program wrapper ------------------------------------------------

struct SchemaField {
  std::string type;                       // "text" | "array" | "object"
  bool required = true;
  std::optional<int> max_length;          // text
  std::vector<std::string> enum_values;   // text
  std::string items_type;                 // array element type
  std::optional<int> items_max_length;    // array
  std::optional<int> length;              // array
  std::optional<int> min_items;
  std::optional<int> max_items;

  std::string hash() const;
};

struct EntryPoint {
  std::string prompt;
  std::map<std::string, SchemaField> inputs;
  std::map<std::string, SchemaField> outputs;

  std::string hash() const;
};

struct PythonImport {
  std::string file;
  std::string target;

  std::string hash() const;
};

/// A full compiled program: entry points, python imports, and prompts. STA is
/// a root artifact (empty provenance, so the hash is a pure content hash).
class STA : public Base<STA> {
  public:
    static constexpr char const * format = "sta";

    std::map<std::string, EntryPoint> entry_points;
    std::map<std::string, PythonImport> python_imports;
    std::map<std::string, Prompt> prompts;

  public:
    std::string content_hash() const;
};

}

#endif // AUTOCOG_DATA_STA_HXX
