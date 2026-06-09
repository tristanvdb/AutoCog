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

  protected:
    std::pair<Model &, ContextID> restore(PathState & state) const;

    void initial();
    void enqueue(ActionID const action, data::FTTNode & parent, PathState const & current);

    unsigned evaluate_text       (PathState & state);
    unsigned evaluate_completion (PathState & state);
    unsigned evaluate_choice     (PathState & state);

  public:
    Evaluation(EvaluationConfig const & config_, ModelID const model_, data::FTA const & fta_);
    unsigned advance(std::optional<unsigned> max_token_eval);
    data::FTT const & retrieve() const;
};

}

#endif // AUTOCOG_BACKEND_LLAMA_EVALUATION_HXX
