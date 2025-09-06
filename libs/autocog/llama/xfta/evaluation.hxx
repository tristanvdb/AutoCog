#ifndef AUTOCOG_LLAMA_XFTA_EVALUATION_HXX
#define AUTOCOG_LLAMA_XFTA_EVALUATION_HXX

#include "autocog/llama/xfta/types.hxx"

#include <optional>
#include <queue>

namespace autocog::llama::xfta {

class Model;
class FTA;
class FTT;
class Text;
class Completion;
class Choice;

struct PathState {
  ActionID const action;            //< Action to be evaluated next
  FTT & parent;                     //< Previous FTT in the path, results of exploring this state will be added to that tree
  TokenSequence const tokens;       //< Tokens that lead to this state
  std::optional<ContextID> context; //< Context used to evaluate this path

  PathState(ActionID const action_, FTT & parent, std::vector<TokenID> const & tokens_, std::optional<ContextID> context);
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
    FTA const & fta;

    Queue queue;
    FTT * root;

  protected:
    std::pair<Model &, ContextID> restore(PathState & state) const;
    
    void initial();
    void enqueue(ActionID const action, FTT & parent, PathState const & current);

    unsigned evaluate_text       (PathState & state);
    unsigned evaluate_completion (PathState & state);
    unsigned evaluate_choice     (PathState & state);

  public:
    Evaluation(EvaluationConfig const & config_, ModelID const model_, FTA const & fta_);
    ~Evaluation();
    unsigned advance(std::optional<unsigned> max_token_eval);
    FTT const & retrieve() const;
};

}

#endif /* AUTOCOG_LLAMA_XFTA_EVALUATION_HXX */

