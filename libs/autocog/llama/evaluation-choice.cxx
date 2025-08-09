
#include "autocog/llama/evaluation.hxx"
#include "autocog/llama/model.hxx"
#include "autocog/llama/fta.hxx"
#include "autocog/llama/ftt.hxx"

#include <llama.h>

#include <stdexcept>
#include <vector>
#include <algorithm>

#if VERBOSE
#  include <iostream>
#endif

#define DEBUG_Evaluation_evaluate_choice VERBOSE && 0

namespace autocog { namespace llama {

struct ChoiceResult {
  size_t index;
  ProbaSequence logprobs;
  float proba;

  ChoiceResult(
    size_t index_, ProbaSequence logprobs_, float proba_
  ) :
    index(index_), logprobs(logprobs_), proba(proba_)
  {}
};

unsigned Evaluation::evaluate_choice(PathState & state) {
#if DEBUG_Evaluation_evaluate_choice
  std::cerr << "Executing Choice     #" << state.action << std::endl;
#endif
  Choice const & action = this->fta.action(state.action).as<Choice>();
#if DEBUG_Evaluation_evaluate_choice
  std::cerr << " - name: " << action.name << std::endl;
  std::cerr << " - width: " << action.width << std::endl;
  std::cerr << " - number of choices: " << action.choices.size() << std::endl;
#endif
  
  if (action.choices.empty()) {
    throw std::runtime_error("Choice action has no choices");
  }

  if (action.successors.size() != action.choices.size()) {
    throw std::runtime_error("Choice action must have as many successors as choices");
  }
  
  unsigned num_token_eval = 0;
  std::vector<ChoiceResult> results;
  
  // Evaluate ALL choices in full
  for (size_t idx = 0; idx < action.choices.size(); ++idx) {
    auto [model, ctx] = this->restore(state);
#if DEBUG_Evaluation_evaluate_choice
    std::cerr << " - Model   #" << model.id << std::endl;
    std::cerr << " - Context #" << ctx << std::endl;
    std::cerr << " - choice[" << idx << "]:" << std::endl;
    std::cerr << "   - number of tokens: " << action.choices[idx].size() << std::endl;
#endif
    
    // Save current state to restore after evaluation
    TokenSequence saved_tokens = model.get_tokens_const(ctx);

    ProbaSequence logprobs;
    num_token_eval += model.eval_sequences(action.choices[idx], logprobs, ctx);

    float proba = 0.;
    for (float lpb : logprobs) proba += lpb;
    proba = std::exp(-proba/logprobs.size());
#if DEBUG_Evaluation_evaluate_choice
    std::cerr << "   - proba: " << proba << std::endl;
#endif

    results.emplace_back(idx, logprobs, proba);

    state.context.reset(); // TODO remove once context saving/restore/rewind is implemented
  }

  std::sort(results.begin(), results.end(), 
    [](const ChoiceResult& a, const ChoiceResult& b) {
      return a.proba > b.proba;
    });

  unsigned count = 0;
  for (const auto & result : results) {
    auto & choice_tokens = action.choices[result.index];
    FTT & child = state.parent.add(action.id, choice_tokens, result.logprobs);
    child.pruned = ( count > action.width ) || (count > 0 && result.proba < action.threshold);
    if (!child.pruned) {
      this->enqueue(action.successors[result.index], child, state);
    }
    count++;
  }

#if DEBUG_Evaluation_evaluate_choice
  std::cerr << " > evaluated: " << num_token_eval << std::endl;
#endif
  
  return num_token_eval;
}

} }

