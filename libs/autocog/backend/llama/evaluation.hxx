#ifndef AUTOCOG_BACKEND_LLAMA_EVALUATION_HXX
#define AUTOCOG_BACKEND_LLAMA_EVALUATION_HXX

#include "autocog/backend/llama/types.hxx"

#include <optional>
#include <queue>

namespace autocog::runtime::fta { class FTA; class FTT; }

namespace autocog::backend::llama {

class Model;

struct PathState {
  ActionID const action;
  runtime::fta::FTT & parent;
  TokenSequence const tokens;
  std::optional<ContextID> context;

  PathState(ActionID const action_, runtime::fta::FTT & parent, std::vector<TokenID> const & tokens_, std::optional<ContextID> context);
  float proba() const;
};

struct EvaluationConfig {
  bool evaluate_text{true};
};

class Evaluation {
  public:
    using Queue = std::queue<PathState>;
    EvaluationConfig const config;

  private:
    ModelID const model;
    runtime::fta::FTA const & fta;

    Queue queue;
    runtime::fta::FTT * root;

  protected:
    std::pair<Model &, ContextID> restore(PathState & state) const;
    
    void initial();
    void enqueue(ActionID const action, runtime::fta::FTT & parent, PathState const & current);

    unsigned evaluate_text       (PathState & state);
    unsigned evaluate_completion (PathState & state);
    unsigned evaluate_choice     (PathState & state);

  public:
    Evaluation(EvaluationConfig const & config_, ModelID const model_, runtime::fta::FTA const & fta_);
    ~Evaluation();
    unsigned advance(std::optional<unsigned> max_token_eval);
    runtime::fta::FTT const & retrieve() const;
};

}

#endif // AUTOCOG_BACKEND_LLAMA_EVALUATION_HXX
