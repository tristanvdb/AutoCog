#ifndef __AUTOCOG_LLAMA_Evaluation__HXX_
#define __AUTOCOG_LLAMA_Evaluation__HXX_

#include "autocog/llama/types.hxx"

#include <optional>
#include <queue>

namespace autocog {
namespace llama {

class Model;
class FTA;
class FTT;
class Text;
class Completion;
class Choice;

class Beam;

struct PathState {
  ActionID const action;            //< Action to be evaluated next
  FTT & parent;                     //< Previous FTT in the path, reslts of exploring this state will be added to that tree 
  TokenSequence const tokens;       //< Tokens that lead to this state

  std::optional<ContextID> context; //< Context used to evaluate this path

  PathState(ActionID const action_, FTT & parent, std::vector<TokenID> const & tokens_, std::optional<ContextID> context);
};

class Evaluation {
  public:
    using Queue = std::queue<PathState>;

  private:
    ModelID const model;
    FTA const & fta;

    Queue queue;
    FTT * root;

  protected:
    std::pair<Model &, ContextID> restore_context(PathState & state) const;
    
    void initial();
    void enqueue(ActionID const action, FTT & parent, std::vector<TokenID> const & tokens, PathState const & current);

    unsigned evaluate_text       (PathState & state);
    unsigned evaluate_completion (PathState & state);
    unsigned evaluate_choice     (PathState & state);

  public:
    Evaluation(ModelID const model_, FTA const & fta_);
    ~Evaluation();
    unsigned advance(std::optional<unsigned> max_token_eval);
    FTT const & get() const;

  friend void add_beams_to_ftt(
    std::vector<Beam> const & beams,
    Completion const & action,
    PathState & state,
    Evaluation * eval
  );
};

} }

#endif /* __AUTOCOG_LLAMA_Evaluation__HXX_ */

