#include "autocog/backend/llama/manager.hxx"
#include "autocog/backend/llama/evaluation.hxx"
#include "autocog/backend/llama/model.hxx"
#include "autocog/logging.hxx"
#include "autocog/utilities/errors.hxx"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <deque>
#include <limits>
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
  child.pruned  = data::Pruned::No;
  data::Action const & a = fta.actions[id];
  child.uid     = a.uid;
  child.field   = a.field;
  child.indices = a.indices;
  parent.children.push_back(std::move(child));
  return parent.children.back();
}

PathState::PathState(ActionID const action_, data::FTTNode & parent_,
                     TokenSequence const & tokens_, std::optional<ContextID> context_,
                     std::uint64_t seq_) :
  action(action_), parent(parent_), tokens(tokens_), context(context_), seq(seq_)
{}

static MetricKey parse_metric_key(std::string const & name) {
  if (name == "perplexity")  return MetricKey::Perplexity;
  if (name == "probability") return MetricKey::Probability;
  if (name == "shortest")    return MetricKey::Shortest;
  if (name == "longest")     return MetricKey::Longest;
  if (name == "shallowest")  return MetricKey::Shallowest;
  if (name == "deepest")     return MetricKey::Deepest;
  if (name == "near_leaf")   return MetricKey::NearLeaf;
  if (name == "far_leaf")    return MetricKey::FarLeaf;
  if (name == "fifo")        return MetricKey::Fifo;
  throw autocog::SchemaError("Unknown queue metric '" + name + "'", name);
}

Evaluation::Evaluation(EvaluationConfig const & config_, ModelID const model_, data::FTA const & fta_) :
  config(config_),
  model(model_),
  prepared(prepare(model_, fta_)),
  result(),
  queue(),
  started(false)
{
  perf_.prepare_seconds = prepared.prepare_seconds;

  for (auto const & name : fta_.queue_metric) metric_.push_back(parse_metric_key(name));
  metric_.push_back(MetricKey::Fifo);  // total, deterministic order

  // Static FTA-graph distances for the depth/leaf keys: forward BFS from the
  // entry, and backward BFS from the terminal actions (empty successors).
  size_t const n = prepared.actions.size();
  unsigned const inf = std::numeric_limits<unsigned>::max();
  depth_.assign(n, inf);
  to_leaf_.assign(n, inf);
  if (n > 0) {
    std::deque<unsigned> bfs{0u};
    depth_[0] = 0;
    while (!bfs.empty()) {
      unsigned const a = bfs.front(); bfs.pop_front();
      for (unsigned s : prepared.actions[a].successors)
        if (depth_[s] == inf) { depth_[s] = depth_[a] + 1; bfs.push_back(s); }
    }
    std::vector<std::vector<unsigned>> preds(n);
    for (unsigned a = 0; a < n; ++a) {
      if (prepared.actions[a].successors.empty()) { to_leaf_[a] = 0; bfs.push_back(a); }
      for (unsigned s : prepared.actions[a].successors) preds[s].push_back(a);
    }
    while (!bfs.empty()) {
      unsigned const a = bfs.front(); bfs.pop_front();
      for (unsigned p : preds[a])
        if (to_leaf_[p] == inf) { to_leaf_[p] = to_leaf_[a] + 1; bfs.push_back(p); }
    }
  }
}

double Evaluation::key_value(MetricKey key, PathState const & s) const {
  data::FTTNode const & node = s.parent;
  switch (key) {
    case MetricKey::Perplexity:
      return node.length ? std::exp(-static_cast<double>(node.logprob) / node.length) : 0.0;
    case MetricKey::Probability: return -static_cast<double>(node.logprob);
    case MetricKey::Shortest:    return -static_cast<double>(node.length);
    case MetricKey::Longest:     return  static_cast<double>(node.length);
    case MetricKey::Shallowest:  return -static_cast<double>(depth_[s.action]);
    case MetricKey::Deepest:     return  static_cast<double>(depth_[s.action]);
    case MetricKey::NearLeaf:    return -static_cast<double>(to_leaf_[s.action]);
    case MetricKey::FarLeaf:     return  static_cast<double>(to_leaf_[s.action]);
    case MetricKey::Fifo:        return -static_cast<double>(s.seq);
  }
  return 0.0;
}

bool Evaluation::worse(PathState const & a, PathState const & b) const {
  for (MetricKey key : metric_) {
    double const va = key_value(key, a);
    double const vb = key_value(key, b);
    if (va < vb) return true;
    if (va > vb) return false;
  }
  return false;
}

void Evaluation::push_state(std::unique_ptr<PathState> state) {
  queue.push_back(std::move(state));
  std::push_heap(queue.begin(), queue.end(),
                 [this](auto const & a, auto const & b) { return worse(*a, *b); });
}

std::unique_ptr<PathState> Evaluation::pop_state() {
  std::pop_heap(queue.begin(), queue.end(),
                [this](auto const & a, auto const & b) { return worse(*a, *b); });
  std::unique_ptr<PathState> state = std::move(queue.back());
  queue.pop_back();
  return state;
}

unsigned Evaluation::advance(std::optional<unsigned> max_token_eval) {
  using clock = std::chrono::steady_clock;
  auto const advance_start = clock::now();
  if (!started) { this->initial(); started = true; }

  unsigned num_token_eval = 0;
  while (!queue.empty() && (max_token_eval == std::nullopt || num_token_eval < max_token_eval)) {
    // Pop before evaluating: the evaluators enqueue successor states, which
    // reorders the heap under any reference into it.
    std::unique_ptr<PathState> const current = this->pop_state();
    PathState & state = *current;
    auto const t0 = clock::now();
    switch (prepared.fta.actions[state.action].body.index()) {
      case 0: {  // TextAction
        num_token_eval += this->evaluate_text(state);
        perf_.text.calls += 1;
        perf_.text.seconds += std::chrono::duration<double>(clock::now() - t0).count();
        break;
      }
      case 1: {  // CompleteAction
        num_token_eval += this->evaluate_completion(state);
        perf_.complete.calls += 1;
        perf_.complete.seconds += std::chrono::duration<double>(clock::now() - t0).count();
        break;
      }
      case 2: {  // ChooseAction
        num_token_eval += this->evaluate_choice(state);
        perf_.choose.calls += 1;
        perf_.choose.seconds += std::chrono::duration<double>(clock::now() - t0).count();
        break;
      }
    }
  }
  perf_.advance_seconds += std::chrono::duration<double>(clock::now() - advance_start).count();
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
    this->push_state(std::make_unique<PathState>(prepared.actions[0].successors[0], result.root,
                                                 init_tokens, std::nullopt, seq_counter_++));
}

void Evaluation::enqueue(ActionID const action, data::FTTNode & parent, PathState const & state) {
  std::optional<ContextID> ctx = state.context;
  ctx.reset(); // TODO context saving logic
  std::vector<TokenID> tokens(state.tokens.begin(), state.tokens.end());
  tokens.insert(tokens.end(), parent.tokens.begin(), parent.tokens.end());
  this->push_state(std::make_unique<PathState>(action, parent, tokens, ctx, seq_counter_++));
}

std::pair<Model &, ContextID> Evaluation::restore(PathState & state, PerfCounters::KindStats & stats) {
  Model & model = Manager::get_model(this->model);
  if (!state.context) state.context = 0;
  // Every action starts scoring or sampling from the restored prefix's
  // final-position distribution, so restoring always primes the logits.
  stats.tokens_restore += model.set_tokens(state.tokens, state.context.value(), /*prime_logits=*/true);
  return std::pair<Model &, ContextID>(model, state.context.value());
}

}
