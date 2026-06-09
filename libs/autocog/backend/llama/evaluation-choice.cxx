#include "autocog/backend/llama/evaluation.hxx"
#include "autocog/backend/llama/model.hxx"
#include "autocog/logging.hxx"
#include "autocog/utilities/exception.hxx"

#include <algorithm>
#include <cmath>
#include <variant>
#include <vector>

namespace autocog::backend::llama {

struct ChoiceResult {
  size_t index;
  ProbaSequence logprobs;
  float proba;
  ChoiceResult(size_t index_, ProbaSequence logprobs_, float proba_)
    : index(index_), logprobs(logprobs_), proba(proba_) {}
};

unsigned Evaluation::evaluate_choice(PathState & state) {
  data::ChooseAction const & ca = std::get<data::ChooseAction>(prepared.fta.actions[state.action].body);
  PreparedAction const & p = prepared.actions[state.action];

  if (p.choices.empty())
    throw autocog::utilities::InternalError("Choice action has no choices");
  if (p.successors.size() != p.choices.size())
    throw autocog::utilities::InternalError("Choice action must have as many successors as choices");

  unsigned num_token_eval = 0;
  std::vector<ChoiceResult> results;

  for (size_t idx = 0; idx < p.choices.size(); ++idx) {
    auto [model, ctx] = this->restore(state);
    ProbaSequence logprobs;
    num_token_eval += model.eval_sequences(p.choices[idx], logprobs, ctx);

    float proba = 0.;
    for (float lpb : logprobs) proba += lpb;
    proba = logprobs.empty() ? 0.0f : std::exp(-proba / logprobs.size());

    results.emplace_back(idx, logprobs, proba);
    state.context.reset(); // TODO remove once context saving/restore/rewind is implemented
  }

  std::sort(results.begin(), results.end(),
    [](const ChoiceResult& a, const ChoiceResult& b) { return a.proba > b.proba; });

  unsigned count = 0;
  for (const auto & result : results) {
    auto & choice_tokens = p.choices[result.index];
    data::FTTNode & child = grow(state.parent, state.action, prepared.fta, choice_tokens, result.logprobs);
    child.pruned = (count >= ca.width) || (count > 0 && result.proba < ca.threshold);
    if (!child.pruned) {
      this->enqueue(p.successors[result.index], child, state);
    }
    count++;
  }
  return num_token_eval;
}

}
