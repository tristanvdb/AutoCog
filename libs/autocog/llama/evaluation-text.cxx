
#include "autocog/llama/manager.hxx"
#include "autocog/llama/evaluation.hxx"
#include "autocog/llama/model.hxx"
#include "autocog/llama/fta.hxx"
#include "autocog/llama/ftt.hxx"

#include <stdexcept>

namespace autocog { namespace llama {

unsigned Evaluation::evaluate_text(PathState & state) {
  auto [model,ctx] = this->restore_context(state);
  Text const & action = this->fta.action(state.action).as<Text>();

  ProbaSequence probas(action.tokens.size(), 1.);
  unsigned num_token_eval = model.eval_sequences(action.tokens, probas, ctx);

  float probability = 1.;
  for (auto proba: probas) probability *= proba;
  state.parent.add(action.id, action.tokens, probas, probability);  

  this->enqueue(action.successors[0], state.parent.children.back(), model.get_tokens_const(), state);
  
  return num_token_eval;
}

} }

