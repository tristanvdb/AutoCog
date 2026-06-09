#include "autocog/backend/llama/evaluation.hxx"
#include "autocog/backend/llama/model.hxx"
#include "autocog/logging.hxx"
#include "autocog/utilities/exception.hxx"

#include <variant>

namespace autocog::backend::llama {

unsigned Evaluation::evaluate_text(PathState & state) {
  data::TextAction const & ta = std::get<data::TextAction>(prepared.fta.actions[state.action].body);
  PreparedAction const & p = prepared.actions[state.action];

  unsigned num_token_eval = 0;
  ProbaSequence logprobs(p.tokens.size(), 0.);
  if (ta.evaluate) {
    auto [model, ctx] = this->restore(state);
    num_token_eval += model.eval_sequences(p.tokens, logprobs, ctx);
  }

  auto & child = grow(state.parent, state.action, prepared.fta, p.tokens, logprobs);
  if (p.successors.size() == 1) {
    this->enqueue(p.successors[0], child, state);
  } else if (p.successors.size() > 1) {
    throw autocog::utilities::InternalError("Text action should never have more than 1 successor");
  }
  return num_token_eval;
}

}
