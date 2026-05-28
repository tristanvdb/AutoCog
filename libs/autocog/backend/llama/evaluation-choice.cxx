
#include "autocog/backend/llama/evaluation.hxx"
#include "autocog/backend/llama/model.hxx"
#include "autocog/runtime/fta/fta.hxx"
#include "autocog/runtime/fta/ftt.hxx"
#include "autocog/logging.hxx"

#include <llama.h>


#include <cmath>
#include "autocog/utilities/exception.hxx"
#include <vector>
#include <algorithm>



namespace autocog::backend::llama {

using namespace autocog::runtime::fta;

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
  SPDLOG_LOGGER_TRACE(autocog::log(), "Executing Choice     #");
  Choice const & action = this->fta.action(state.action).as<Choice>();
  SPDLOG_LOGGER_TRACE(autocog::log(), " - name:");
  SPDLOG_LOGGER_TRACE(autocog::log(), " - width:");
  SPDLOG_LOGGER_TRACE(autocog::log(), " - number of choices:");
  
  if (action.choices.empty()) {
    throw autocog::utilities::InternalError("Choice action has no choices");
  }

  if (action.successors.size() != action.choices.size()) {
    throw autocog::utilities::InternalError("Choice action must have as many successors as choices");
  }
  
  unsigned num_token_eval = 0;
  std::vector<ChoiceResult> results;
  
  // Evaluate ALL choices in full
  for (size_t idx = 0; idx < action.choices.size(); ++idx) {
    auto [model, ctx] = this->restore(state);
  SPDLOG_LOGGER_TRACE(autocog::log(), " - Model   #");
  SPDLOG_LOGGER_TRACE(autocog::log(), " - Context #");
  SPDLOG_LOGGER_TRACE(autocog::log(), " - choice[ ]:");
  SPDLOG_LOGGER_TRACE(autocog::log(), "   - number of tokens:");
    
    // Save current state to restore after evaluation
    TokenSequence saved_tokens = model.get_tokens_const(ctx);

    ProbaSequence logprobs;
    num_token_eval += model.eval_sequences(action.choices[idx], logprobs, ctx);

    float proba = 0.;
    for (float lpb : logprobs) proba += lpb;
    proba = std::exp(-proba/logprobs.size());
  SPDLOG_LOGGER_TRACE(autocog::log(), "   - proba:");

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
    child.pruned = ( count >= action.width ) || (count > 0 && result.proba < action.threshold);
    if (!child.pruned) {
      this->enqueue(action.successors[result.index], child, state);
    }
    count++;
  }

  SPDLOG_LOGGER_TRACE(autocog::log(), " > evaluated:");
  
  return num_token_eval;
}

}

