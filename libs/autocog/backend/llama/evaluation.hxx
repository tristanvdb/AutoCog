#ifndef AUTOCOG_BACKEND_LLAMA_EVALUATION_HXX
#define AUTOCOG_BACKEND_LLAMA_EVALUATION_HXX

#include "autocog/backend/llama/types.hxx"
#include "autocog/backend/llama/prepared.hxx"

#include "autocog/data/fta.hxx"
#include "autocog/data/ftt.hxx"

#include <optional>
#include <queue>

namespace autocog::backend::llama {

class Model;

struct PathState {
  ActionID const action;
  data::FTTNode & parent;
  TokenSequence const tokens;
  std::optional<ContextID> context;

  PathState(ActionID const action_, data::FTTNode & parent,
            std::vector<TokenID> const & tokens_, std::optional<ContextID> context);
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

// Append a child to `parent`: cumulative logprob/length, and the at-creation
// enrichment (uid/field/indices) from the FTA action. `text` is filled later.
data::FTTNode & grow(data::FTTNode & parent, ActionID const id, data::FTA const & fta,
                     TokenSequence const & tokens, ProbaSequence const & logprobs);

class Evaluation {
  public:
    using Queue = std::queue<PathState>;
    EvaluationConfig const config;

  private:
    ModelID const model;
    PreparedFTA prepared;      // model-bound tokenization over the portable FTA
    data::FTT result;          // the tree we grow in place (result.root is the root)

    Queue queue;
    bool started{false};
    PerfCounters perf_;

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
};

}

#endif // AUTOCOG_BACKEND_LLAMA_EVALUATION_HXX
