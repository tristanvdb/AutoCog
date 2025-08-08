
#include "autocog/llama/manager.hxx"
#include "autocog/llama/evaluation.hxx"
#include "autocog/llama/model.hxx"
#include "autocog/llama/ftt.hxx"
#include "autocog/llama/fta.hxx"

#if VERBOSE
#  include <iostream>
#endif

#define DEBUG_Evaluation_enqueue VERBOSE && 0
#define DEBUG_Evaluation_advance VERBOSE && 1

namespace autocog { namespace llama {

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
#if DEBUG_Evaluation_advance
    std::cerr << "Evaluation::advance" << std::endl;
    std::cerr << "  queue.size()         = " << queue.size() << std::endl;
    std::cerr << "  num_action_eval      = " << num_action_eval << std::endl;
    std::cerr << "  num_token_eval       = " << num_token_eval << std::endl;
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
  if (state.context) {
    throw std::runtime_error("Context saving is not implemented yet, this should not happen");
  } else {
    // for now we always use context 0 and reset it before evaluation any action
    model.set_tokens(state.tokens, 0);
    state.context = 0;
  }
  return std::pair<Model &, ContextID>(model, state.context.value());
}

} }

