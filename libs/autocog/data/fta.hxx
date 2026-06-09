#ifndef AUTOCOG_DATA_FTA_HXX
#define AUTOCOG_DATA_FTA_HXX

#include "autocog/data/base.hxx"
#include "autocog/data/vocab.hxx"

#include <map>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace autocog::data {

/// Emits fixed text, optionally evaluated against the running state.
struct TextAction {
  std::string text;
  bool evaluate = false;
};

/// Free generation under search params, optionally masked by a vocab.
struct CompleteAction {
  unsigned length = 0;
  float threshold = 0.0f;
  unsigned beams = 0;
  unsigned ahead = 0;
  unsigned width = 0;
  std::string stop_text;
  std::optional<float> repetition;
  std::optional<float> diversity;
  std::optional<std::string> vocab;   ///< Reference into FTA::vocabs.
};

/// Constrained selection among a fixed list of choices.
struct ChooseAction {
  std::vector<std::string> choices;
  float threshold = 0.0f;
  unsigned width = 0;
};

/// One action (node) of the FTA DAG: the common fields plus a typed body. The
/// wire form is flat (the body's fields are siblings of uid/successors, with a
/// "type" discriminant). Conversion lives in the free functions in json.hxx /
/// python.hxx.
struct Action {
  std::string uid;                              ///< Node identity; successors reference it.
  std::vector<std::string> successors;          ///< Out-edges; index-aligned with choices.
  std::optional<int> field;                     ///< Schema field index (value nodes).
  std::optional<std::vector<int>> indices;      ///< Nested field indices, when present.
  std::variant<TextAction, CompleteAction, ChooseAction> body;

  std::string hash() const;
};

/// A finite thought automaton: a DAG of actions (edges by successor uid, entry
/// at actions[0]), a queue metric, and the vocab table the complete actions
/// reference.
class FTA : public Base<FTA> {
  public:
    static constexpr char const * format = "fta";

    std::vector<Action> actions;                ///< actions[0] is the entry point.
    std::string queue_metric;
    std::map<std::string, VocabExpr> vocabs;    ///< ref -> expression tree.

  public:
    std::string content_hash() const;
};

}

#endif // AUTOCOG_DATA_FTA_HXX
