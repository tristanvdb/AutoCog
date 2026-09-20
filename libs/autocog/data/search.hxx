#ifndef AUTOCOG_DATA_SEARCH_HXX
#define AUTOCOG_DATA_SEARCH_HXX

#include "autocog/data/base.hxx"
#include "autocog/data/expr.hxx"

#include <optional>
#include <string>
#include <vector>

namespace autocog::data {

/// Parameters of a `complete` (text) action.
struct TextSearch {
  float threshold = 0.0f;
  unsigned beams = 0;
  std::optional<unsigned> topk;  ///< Candidate tokens sampled per beam expansion.
                                 ///< Unset = `beams` (the historical implicit tie).
  unsigned ahead = 0;
  unsigned width = 0;
  std::optional<float> repetition;  ///< Unset = not used (avoids a near-zero float).
  std::optional<float> diversity;   ///< Unset = not used.
};

/// Parameters of a `choose` action; the same shape governs enum / branch / flow.
/// Candidates are scored by a metric ("mean" per-token geometric mean, "sum"
/// joint log-probability, "bytes" per-byte): `ranking` orders the candidates,
/// `threshold`+`threshold_metric` prune them. The two metrics are independent
/// so a ranking sweep does not silently change how much of the tree survives.
struct ChoiceSearch {
  float threshold = 0.0f;
  unsigned width = 0;
  std::string ranking = "mean";           ///< registry: ranking.metric
  std::string threshold_metric = "mean";  ///< registry: threshold.metric
};

/// Queue-scope (prompt-global) parameters.
struct QueueSearch {
  /// Lexicographic ordering keys for the evaluation queue (perplexity,
  /// probability, shortest/longest, shallowest/deepest, near_leaf/far_leaf,
  /// fifo); earlier entries dominate, "fifo" is the implicit final tie-break.
  std::vector<std::string> metric;
  /// Early-termination predicate; absent = run the queue to exhaustion. An
  /// ordinary expression over the __status__ and __model__ namespaces: the
  /// model properties fold when the evaluation is set up, so the hot loop only
  /// ever compares numbers.
  std::optional<Expr> stop;
};

/// Search configuration: per-category search parameters. Conversion lives in
/// the free functions in json.hxx / python.hxx.
/// The configured value of a search parameter, named as the registry names it
/// ("text.beams", "enum.threshold.value"). This is what `__search__.<cat>.<key>`
/// reads: a policy that references its own parameter tracks the config default,
/// and one that computes from it (`__search__.text.beams * 2`) can.
/// nullopt when the parameter is unset (an optional one) or is not a scalar
/// (queue.stop is a predicate, and a predicate is not a value).
std::optional<Value> search_get(class SearchConfig const & cfg,
                                std::string const & category,
                                std::string const & key);

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
