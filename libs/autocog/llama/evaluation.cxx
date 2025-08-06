
#include "autocog/llama/manager.hxx"
#include "autocog/llama/evaluation.hxx"
#include "autocog/llama/model.hxx"
#include "autocog/llama/ftt.hxx"
#include "autocog/llama/fta.hxx"

#include <iostream>

namespace autocog { namespace llama {

PathState::PathState(ActionID const action_, FTT & parent_, TokenSequence const & tokens_, std::optional<ContextID> context_) :
  action(action_),
  parent(parent_),
  tokens(tokens_),
  context(context_)
{}

Evaluation::Evaluation(ModelID const model_, FTA const & fta_) :
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

  unsigned num_token_eval = 0;
  while (!queue.empty() && (max_token_eval == std::nullopt || num_token_eval < max_token_eval)) {
    PathState & state = queue.front();
    Action const & action = this->fta.action(state.action); 
    switch (action.kind) {
      case ActionKind::Text:
        std::cerr << "Executing Text       #" << action.id << std::endl;
        num_token_eval += this->evaluate_text(state);
        break;
      case ActionKind::Completion:
        std::cerr << "Executing Completion #" << action.id << std::endl;
        num_token_eval += this->evaluate_completion(state);
        break;
      case ActionKind::Choice:
        std::cerr << "Executing Choice     #" << action.id << std::endl;
        num_token_eval += this->evaluate_choice(state);
        break;
    }
    queue.pop();
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
  ProbaSequence probas(init.tokens.size(), 1.);
  float probability = 1.;
  for (auto proba: probas) probability *= proba;

  this->root = new FTT(0, init.tokens, probas, probability);
  this->queue.emplace(init.successors[0], *(this->root), init.tokens, std::nullopt);
}

std::pair<Model &, ContextID> Evaluation::restore_context(PathState & state) const {
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

void Evaluation::enqueue(ActionID const action, FTT & parent, std::vector<TokenID> const & tokens, PathState const & state) {
  std::optional<ContextID> ctx = state.context;
  ctx.reset(); // TODO context saving logic
  this->queue.emplace(action, parent, tokens, ctx);
}

std::vector<float> softmax(float * logits, int vocab_size) {
    std::vector<float> result(logits, logits + vocab_size);
    
    // Apply softmax
    float max_logit = *std::max_element(result.begin(), result.end());
    float sum = 0.0f;
    for (float& logit : result) {
        logit = std::exp(logit - max_logit);
        sum += logit;
    }
    for (float& prob : result) {
        prob /= sum;
    }
    
    return result;
}

} }

