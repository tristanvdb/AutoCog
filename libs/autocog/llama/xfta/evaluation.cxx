
#include "autocog/llama/xfta/manager.hxx"
#include "autocog/llama/xfta/evaluation.hxx"
#include "autocog/llama/xfta/model.hxx"
#include "autocog/llama/xfta/ftt.hxx"
#include "autocog/llama/xfta/fta.hxx"

#if VERBOSE
#  include <iostream>
#endif

#define DEBUG_Evaluation_enqueue VERBOSE && 0
#define DEBUG_Evaluation_advance VERBOSE && 0

namespace autocog::llama::xfta {

PathState::PathState(ActionID const action_, FTT & parent_, TokenSequence const & tokens_, std::optional<ContextID> context_) :
  action(action_),
  parent(parent_),
  tokens(tokens_),
  context(context_)
{}

float PathState::proba() const {
  return this->parent.proba();
}

Evaluation::Evaluation(EvaluationConfig const & config_, ModelID const model_, FTA const & fta_) :
  config(config_),
  model(model_),
  fta(fta_),
  queue(),
  root(nullptr)
{}

Evaluation::~Evaluation() {
  if (this->root)
    delete this->root;
}

unsigned Evaluation::advance(std::optional<unsigned> max_token_eval) {
  if (this->root == nullptr) this->initial();

  unsigned num_action_eval = 0;
  unsigned num_token_eval = 0;
  while (!queue.empty() && (max_token_eval == std::nullopt || num_token_eval < max_token_eval)) {
    PathState & state = queue.front();
#if VERBOSE
    std::cerr << "Evaluation::advance [ Q=" << queue.size() << ", A=" << num_action_eval << ", T=" << num_token_eval << " ]" << std::endl;
#endif
#if DEBUG_Evaluation_advance
    std::cerr << "  state.action         = " << state.action << std::endl;
    std::cerr << "  state.tokens.size()  = " << state.tokens.size() << std::endl;
    std::cerr << "  state.proba()        = " << state.proba() << std::endl;
    std::cerr << "  state.parent.length  = " << state.parent.length << std::endl;
    std::cerr << "  state.parent.logprob = " << state.parent.logprob << std::endl;
#endif
    Action const & action = this->fta.action(state.action);
    switch (action.kind) {
      case ActionKind::Text:
        num_token_eval += this->evaluate_text(state);
        break;
      case ActionKind::Completion:
        num_token_eval += this->evaluate_completion(state);
        break;
      case ActionKind::Choice:
        num_token_eval += this->evaluate_choice(state);
        break;
    }
    queue.pop();
    num_action_eval++;
  }

  return num_token_eval;
}

FTT const & Evaluation::get() const {
  return *(this->root);
}

void Evaluation::initial() {
  Text const & init = this->fta.action(0).as<Text>();
  TokenSequence tokens = init.tokens;
  TokenID bos = Manager::get_model(this->model).bos_token();
  if (tokens[0] != bos) {
    tokens.insert(tokens.begin(), bos);
  }
  this->root = FTT::make_root(init.tokens);
  this->queue.emplace(init.successors[0], *(this->root), init.tokens, std::nullopt);
}

void Evaluation::enqueue(
  ActionID const action,
  FTT & parent,
  PathState const & state
) {
#if DEBUG_Evaluation_enqueue
  std::cerr << ">> Evaluation::enqueue <<" << std::endl;
#endif
  std::optional<ContextID> ctx = state.context;
  ctx.reset(); // TODO context saving logic
  
  std::vector<TokenID> tokens(state.tokens.begin(), state.tokens.end());
  tokens.insert(tokens.end(), parent.tokens.begin(), parent.tokens.end());

  this->queue.emplace(action, parent, tokens, ctx);
}

std::pair<Model &, ContextID> Evaluation::restore(PathState & state) const {
  Model & model = Manager::get_model(this->model);

  if (!state.context) {
    state.context = 0; // TODO look at existing context for the largest prefix? 
  }
  model.set_tokens(state.tokens, state.context.value());

  return std::pair<Model &, ContextID>(model, state.context.value());
}

}

