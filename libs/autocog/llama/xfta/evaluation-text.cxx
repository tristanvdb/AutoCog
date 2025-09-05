
#include "autocog/llama/xfta/manager.hxx"
#include "autocog/llama/xfta/evaluation.hxx"
#include "autocog/llama/xfta/model.hxx"
#include "autocog/llama/xfta/fta.hxx"
#include "autocog/llama/xfta/ftt.hxx"

#include <stdexcept>

#if VERBOSE
#  include <iostream>
#endif

#define DEBUG_Evaluation_evaluate_text VERBOSE && 0

namespace autocog::llama::xfta {

unsigned Evaluation::evaluate_text(PathState & state) {
#if DEBUG_Evaluation_evaluate_text
  std::cerr << "Executing Text       #" << state.action << std::endl;
#endif
  Text const & action = this->fta.action(state.action).as<Text>();
#if DEBUG_Evaluation_evaluate_text
  std::cerr << " - name: " << action.name << std::endl;
  std::cerr << " - number of tokens: " << action.tokens.size() << std::endl;
#endif

  unsigned num_token_eval = 0;
  ProbaSequence logprobs(action.tokens.size(), 0.);
  if (action.evaluate) {
    auto [model,ctx] = this->restore(state);
#if DEBUG_Evaluation_evaluate_text
    std::cerr << " - Model   #" << model.id << std::endl;
    std::cerr << " - Context #" << ctx << std::endl;
#endif
    num_token_eval += model.eval_sequences(action.tokens, logprobs, ctx);
  }
#if DEBUG_Evaluation_evaluate_text
  std::cerr << " > evaluated: " << num_token_eval << std::endl;
#endif

  auto & child = state.parent.add(action.id, action.tokens, logprobs);
  if (action.successors.size() == 1) {
    this->enqueue(action.successors[0], child, state);
  } else if (action.successors.size() > 1) {
    throw std::runtime_error("Text action should never have more than 1 successor.");
  }
  
  return num_token_eval;
}

}

