#include "autocog/backend/llama/manager.hxx"
#include "autocog/backend/llama/evaluation.hxx"
#include "autocog/backend/llama/model.hxx"
#include "autocog/logging.hxx"

#include <cmath>
#include <utility>
#include <variant>

namespace autocog::backend::llama {

data::FTTNode & grow(data::FTTNode & parent, ActionID const id, data::FTA const & fta,
                     TokenSequence const & tokens, ProbaSequence const & logprobs) {
  data::FTTNode child;
  child.action = id;
  child.tokens = tokens;
  child.logprobs = logprobs;
  float lp = parent.logprob;
  for (float l : logprobs) lp += l;
  child.logprob = lp;
  child.length  = parent.length + static_cast<unsigned>(tokens.size());
  child.pruned  = false;
  data::Action const & a = fta.actions[id];
  child.uid     = a.uid;
  child.field   = a.field;
  child.indices = a.indices;
  parent.children.push_back(std::move(child));
  return parent.children.back();
}

PathState::PathState(ActionID const action_, data::FTTNode & parent_,
                     TokenSequence const & tokens_, std::optional<ContextID> context_) :
  action(action_), parent(parent_), tokens(tokens_), context(context_)
{}

Evaluation::Evaluation(EvaluationConfig const & config_, ModelID const model_, data::FTA const & fta_) :
  config(config_),
  model(model_),
  prepared(prepare(model_, fta_)),
  result(),
  queue(),
  started(false)
{}

unsigned Evaluation::advance(std::optional<unsigned> max_token_eval) {
  if (!started) { this->initial(); started = true; }

  unsigned num_token_eval = 0;
  while (!queue.empty() && (max_token_eval == std::nullopt || num_token_eval < max_token_eval)) {
    PathState & state = queue.front();
    switch (prepared.fta.actions[state.action].body.index()) {
      case 0: num_token_eval += this->evaluate_text(state);       break;  // TextAction
      case 1: num_token_eval += this->evaluate_completion(state); break;  // CompleteAction
      case 2: num_token_eval += this->evaluate_choice(state);     break;  // ChooseAction
    }
    queue.pop();
  }
  return num_token_eval;
}

data::FTT const & Evaluation::retrieve() const {
  return result;
}

void Evaluation::initial() {
  data::FTA const & fta = prepared.fta;
  TokenSequence const & init_tokens = prepared.actions[0].tokens;
  result.root.action = 0;
  result.root.tokens = init_tokens;
  result.root.logprobs.assign(init_tokens.size(), 0.0f);
  result.root.logprob = 0.0f;
  result.root.length  = static_cast<unsigned>(init_tokens.size());
  data::Action const & a0 = fta.actions[0];
  result.root.uid     = a0.uid;
  result.root.field   = a0.field;
  result.root.indices = a0.indices;
  if (!prepared.actions[0].successors.empty())
    this->queue.emplace(prepared.actions[0].successors[0], result.root, init_tokens, std::nullopt);
}

void Evaluation::enqueue(ActionID const action, data::FTTNode & parent, PathState const & state) {
  std::optional<ContextID> ctx = state.context;
  ctx.reset(); // TODO context saving logic
  std::vector<TokenID> tokens(state.tokens.begin(), state.tokens.end());
  tokens.insert(tokens.end(), parent.tokens.begin(), parent.tokens.end());
  this->queue.emplace(action, parent, tokens, ctx);
}

std::pair<Model &, ContextID> Evaluation::restore(PathState & state) const {
  Model & model = Manager::get_model(this->model);
  if (!state.context) state.context = 0;
  model.set_tokens(state.tokens, state.context.value());
  return std::pair<Model &, ContextID>(model, state.context.value());
}

}
