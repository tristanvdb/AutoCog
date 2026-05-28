
#include "autocog/backend/llama/manager.hxx"
#include "autocog/backend/llama/evaluation.hxx"
#include "autocog/backend/llama/model.hxx"
#include "autocog/runtime/fta/fta.hxx"
#include "autocog/runtime/fta/ftt.hxx"
#include "autocog/logging.hxx"

#include "autocog/utilities/exception.hxx"



namespace autocog::backend::llama {

using namespace autocog::runtime::fta;

unsigned Evaluation::evaluate_text(PathState & state) {
  SPDLOG_LOGGER_TRACE(autocog::log(), "Executing Text       #");
  Text const & action = this->fta.action(state.action).as<Text>();
  SPDLOG_LOGGER_TRACE(autocog::log(), " - name:");
  SPDLOG_LOGGER_TRACE(autocog::log(), " - number of tokens:");

  unsigned num_token_eval = 0;
  ProbaSequence logprobs(action.tokens.size(), 0.);
  if (action.evaluate) {
    auto [model,ctx] = this->restore(state);
  SPDLOG_LOGGER_TRACE(autocog::log(), " - Model   #");
  SPDLOG_LOGGER_TRACE(autocog::log(), " - Context #");
    num_token_eval += model.eval_sequences(action.tokens, logprobs, ctx);
  }
  SPDLOG_LOGGER_TRACE(autocog::log(), " > evaluated:");

  auto & child = state.parent.add(action.id, action.tokens, logprobs);
  if (action.successors.size() == 1) {
    this->enqueue(action.successors[0], child, state);
  } else if (action.successors.size() > 1) {
    throw autocog::utilities::InternalError("Text action should never have more than 1 successor");
  }
  
  return num_token_eval;
}

}

