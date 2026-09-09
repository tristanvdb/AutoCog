#ifndef AUTOCOG_BACKEND_LLAMA_EVALUATION_HXX
#define AUTOCOG_BACKEND_LLAMA_EVALUATION_HXX

#include "autocog/backend/llama/types.hxx"
#include "autocog/backend/llama/prepared.hxx"

#include "autocog/data/fta.hxx"
#include "autocog/data/ftt.hxx"

#include <cstdint>
#include <memory>
#include <optional>
#include <set>
#include <vector>

namespace autocog::backend::llama {

class Model;

struct PathState {
  ActionID const action;
  data::FTTNode & parent;
  TokenSequence const tokens;
  std::optional<ContextID> context;
  std::uint64_t const seq;   ///< Arrival order: the fifo key and final tie-break.

  PathState(ActionID const action_, data::FTTNode & parent,
            std::vector<TokenID> const & tokens_, std::optional<ContextID> context,
            std::uint64_t seq_);
};

/// Queue ordering keys (FTA queue.metric, lexicographic). Every key is
/// evaluated at enqueue-time state: the pending action and the FTT node it
/// extends. Fifo is always the implicit final tie-break, so the ordering is
/// total and deterministic.
enum class MetricKey {
  Perplexity,   ///< best mean per-token probability first
  Probability,  ///< best cumulative path probability first (favors short)
  Shortest, Longest,        ///< token length of the restored prefix
  Shallowest, Deepest,      ///< FTA-graph distance of the action from the entry
  NearLeaf, FarLeaf,        ///< static min distance from the action to any FTA terminal
  Fifo,
};

struct EvaluationConfig {
  bool evaluate_text{true};
};

// Coarse-grain performance counters, accumulated during advance(). Cheap
// enough to be always-on (a few clock reads per action against milliseconds
// of model work); exported by xfta --perf as ECS-flavored NDJSON.
struct PerfCounters {
  struct KindStats {
    unsigned calls = 0;
    double   seconds = 0.0;         ///< wall time inside this action kind
    unsigned tokens_restore = 0;    ///< decoded restoring a branch prefix (set_tokens)
    unsigned tokens_eval = 0;       ///< decoded scoring/generating within the action
  };
  KindStats text, complete, choose;
  unsigned lookahead_tokens = 0;    ///< subset of complete.tokens_eval spent on ahead rollouts
  double   prepare_seconds = 0.0;   ///< prepare(): tokenization + mask priming
  double   advance_seconds = 0.0;   ///< total wall time inside advance()
};

/// Search-progress summary (termination counters), exported by xfta --perf.
struct SearchStats {
  unsigned terminals = 0;      ///< completed (un-pruned, successor-less) FTT paths
  double coverage_fta = 0.0;   ///< fraction of FTA actions evaluated at least once
  double coverage_sta = 0.0;   ///< fraction of distinct schema fields reached
  bool stopped = false;        ///< the stop predicate fired
  unsigned abandoned = 0;      ///< pending subtrees dropped when it fired
};

// Append a child to `parent`: cumulative logprob/length, and the at-creation
// enrichment (uid/field/indices) from the FTA action. `text` is filled later.
data::FTTNode & grow(data::FTTNode & parent, ActionID const id, data::FTA const & fta,
                     TokenSequence const & tokens, ProbaSequence const & logprobs);

class Evaluation {
  public:
    EvaluationConfig const config;

  private:
    ModelID const model;
    PreparedFTA prepared;      // model-bound tokenization over the portable FTA
    data::FTT result;          // the tree we grow in place (result.root is the root)

    // Pending states as a binary heap ordered by the metric list (top = next
    // to evaluate). unique_ptr because PathState holds a reference member and
    // is not assignable; the heap moves pointers, not states.
    std::vector<std::unique_ptr<PathState>> queue;
    std::vector<MetricKey> metric_;      ///< parsed queue.metric + implicit Fifo
    std::vector<unsigned> depth_;        ///< FTA-graph distance from the entry, per action
    std::vector<unsigned> to_leaf_;      ///< min distance to any terminal action
    std::uint64_t seq_counter_ = 0;
    bool started{false};
    PerfCounters perf_;

    /// Key value under one key; larger is better (ascending keys are negated).
    double key_value(MetricKey key, ActionID action, data::FTTNode const & node,
                     std::uint64_t seq) const;
    bool worse(PathState const & a, PathState const & b) const;
    void push_state(std::unique_ptr<PathState> state);
    std::unique_ptr<PathState> pop_state();

    // Termination: counters incrementally maintained by advance(), scalar
    // lookup for the stop predicate, and the predicate itself (validated at
    // construction so unknown scalars fail before any model work).
    unsigned terminals_ = 0;
    double proba_sum_ = 0.0, proba_sq_sum_ = 0.0;   ///< over terminal probas
    data::FTTNode const * best_terminal_ = nullptr; ///< best by the metric list
    ActionID best_terminal_action_ = 0;
    std::vector<bool> visited_actions_;
    std::set<int> visited_fields_;
    unsigned total_fields_ = 0;
    unsigned tokens_total_ = 0;
    bool stopped_ = false;
    unsigned abandoned_ = 0;

    void on_terminal(data::FTTNode const & node, ActionID action);
    double scalar_value(std::string const & name, double seconds_now) const;
    bool eval_stop(data::TermExpr const & e, double seconds_now) const;

  protected:
    // Restore the branch prefix into the state's context; tokens decoded doing
    // so are charged to `stats.tokens_restore`.
    std::pair<Model &, ContextID> restore(PathState & state, PerfCounters::KindStats & stats);

    void initial();
    void enqueue(ActionID const action, data::FTTNode & parent, PathState const & current);

    unsigned evaluate_text       (PathState & state);
    unsigned evaluate_completion (PathState & state);
    unsigned evaluate_choice     (PathState & state);

  public:
    Evaluation(EvaluationConfig const & config_, ModelID const model_, data::FTA const & fta_);
    unsigned advance(std::optional<unsigned> max_token_eval);
    data::FTT const & retrieve() const;
    PerfCounters const & perf() const { return perf_; }
    SearchStats search_stats() const;
};

}

#endif // AUTOCOG_BACKEND_LLAMA_EVALUATION_HXX
