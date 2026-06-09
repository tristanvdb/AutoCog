#ifndef AUTOCOG_DATA_SEARCH_HXX
#define AUTOCOG_DATA_SEARCH_HXX

#include "autocog/data/base.hxx"

#include <optional>
#include <string>

namespace autocog::data {

/// Parameters of a `complete` (text) action.
struct TextSearch {
  float threshold = 0.0f;
  unsigned beams = 0;
  unsigned ahead = 0;
  unsigned width = 0;
  std::optional<float> repetition;  ///< Unset = not used (avoids a near-zero float).
  std::optional<float> diversity;   ///< Unset = not used.
};

/// Parameters of a `choose` action; the same shape governs enum / branch / flow.
struct ChoiceSearch {
  float threshold = 0.0f;
  unsigned width = 0;
};

/// Queue-scope (prompt-global) parameters.
struct QueueSearch {
  std::string metric;
};

/// Search configuration: per-category search parameters. Conversion lives in
/// the free functions in json.hxx / python.hxx.
class SearchConfig : public Base<SearchConfig> {
  public:
    static constexpr char const * format = "search";

    TextSearch   text;
    ChoiceSearch enums;   // JSON section "enum" (enum is a keyword)
    ChoiceSearch branch;
    ChoiceSearch flow;
    QueueSearch  queue;

  public:
    std::string content_hash() const;
};

}

#endif // AUTOCOG_DATA_SEARCH_HXX
