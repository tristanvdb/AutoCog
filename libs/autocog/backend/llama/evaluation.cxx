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

static bool known_stop_scalar(std::string const & name) {
  return name == "terminals" || name == "coverage.fta" || name == "coverage.sta"
      || name == "best.proba" || name == "mean.proba" || name == "std.proba"
      || name == "best.zscore" || name == "tokens" || name == "queue.size"
      || name == "seconds";
}

static void validate_stop(data::TermExpr const & e) {
  if (!e.scalar.empty() && !known_stop_scalar(e.scalar))
    throw autocog::SchemaError("Unknown stop scalar '" + e.scalar + "'", e.scalar);
  for (auto const & op : e.operands) validate_stop(op);
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

  if (fta_.queue_stop) validate_stop(*fta_.queue_stop);  // fail before model work
  visited_actions_.assign(prepared.actions.size(), false);
  std::set<int> fields;
  for (auto const & a : fta_.actions) if (a.field) fields.insert(*a.field);
  total_fields_ = static_cast<unsigned>(fields.size());

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

double Evaluation::key_value(MetricKey key, ActionID action, data::FTTNode const & node,
                             std::uint64_t seq) const {
  switch (key) {
    case MetricKey::Perplexity:
      return node.length ? std::exp(-static_cast<double>(node.logprob) / node.length) : 0.0;
    case MetricKey::Probability: return -static_cast<double>(node.logprob);
    case MetricKey::Shortest:    return -static_cast<double>(node.length);
    case MetricKey::Longest:     return  static_cast<double>(node.length);
    case MetricKey::Shallowest:  return -static_cast<double>(depth_[action]);
    case MetricKey::Deepest:     return  static_cast<double>(depth_[action]);
    case MetricKey::NearLeaf:    return -static_cast<double>(to_leaf_[action]);
    case MetricKey::FarLeaf:     return  static_cast<double>(to_leaf_[action]);
    case MetricKey::Fifo:        return -static_cast<double>(seq);
  }
  return 0.0;
}

bool Evaluation::worse(PathState const & a, PathState const & b) const {
  for (MetricKey key : metric_) {
    double const va = key_value(key, a.action, a.parent, a.seq);
    double const vb = key_value(key, b.action, b.parent, b.seq);
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

void Evaluation::on_terminal(data::FTTNode const & node, ActionID action) {
  terminals_ += 1;
  double const proba = node.length ? std::exp(-static_cast<double>(node.logprob) / node.length) : 0.0;
  proba_sum_ += proba;
  proba_sq_sum_ += proba * proba;
  // "Best" reuses the queue's metric list, so ordering and stopping agree;
  // the fifo tie-break (registration order) keeps the first of equals.
  bool better = best_terminal_ == nullptr;
  if (!better) {
    for (MetricKey key : metric_) {
      double const vn = key_value(key, action, node, terminals_);
      double const vb = key_value(key, best_terminal_action_, *best_terminal_, 0);
      if (vn > vb) { better = true; break; }
      if (vn < vb) break;
    }
  }
  if (better) {
    best_terminal_ = &node;   // stable: FTTNode children live in a std::list
    best_terminal_action_ = action;
  }
}

double Evaluation::scalar_value(std::string const & name, double seconds_now) const {
  double const n = static_cast<double>(terminals_);
  double const mean = terminals_ ? proba_sum_ / n : 0.0;
  if (name == "terminals")    return n;
  if (name == "coverage.fta") {
    size_t const total = visited_actions_.size();
    if (!total) return 1.0;
    size_t visited = 0;
    for (bool v : visited_actions_) visited += v;
    return static_cast<double>(visited) / static_cast<double>(total);
  }
  if (name == "coverage.sta")
    return total_fields_ ? static_cast<double>(visited_fields_.size()) / total_fields_ : 1.0;
  if (name == "best.proba")
    return best_terminal_ && best_terminal_->length
        ? std::exp(-static_cast<double>(best_terminal_->logprob) / best_terminal_->length) : 0.0;
  if (name == "mean.proba") return mean;
  if (name == "std.proba" || name == "best.zscore") {
    double const var = terminals_ ? proba_sq_sum_ / n - mean * mean : 0.0;
    double const sd = var > 0.0 ? std::sqrt(var) : 0.0;
    if (name == "std.proba") return sd;
    if (terminals_ < 3 || sd <= 0.0) return 0.0;   // undefined on a tiny field
    return (scalar_value("best.proba", seconds_now) - mean) / sd;
  }
  if (name == "tokens")     return static_cast<double>(tokens_total_);
  if (name == "queue.size") return static_cast<double>(queue.size());
  if (name == "seconds")    return seconds_now;
  return 0.0;  // unreachable: validated at construction
}

bool Evaluation::eval_stop(data::TermExpr const & e, double seconds_now) const {
  using Kind = data::TermExpr::Kind;
  switch (e.kind) {
    case Kind::All:
      for (auto const & op : e.operands) if (!eval_stop(op, seconds_now)) return false;
      return true;
    case Kind::Any:
      for (auto const & op : e.operands) if (eval_stop(op, seconds_now)) return true;
      return false;
    case Kind::Not:
      return e.operands.empty() ? true : !eval_stop(e.operands[0], seconds_now);
    case Kind::Ge: return scalar_value(e.scalar, seconds_now) >= e.value;
    case Kind::Gt: return scalar_value(e.scalar, seconds_now) >  e.value;
    case Kind::Le: return scalar_value(e.scalar, seconds_now) <= e.value;
    case Kind::Lt: return scalar_value(e.scalar, seconds_now) <  e.value;
  }
  return false;
}

SearchStats Evaluation::search_stats() const {
  SearchStats s;
  s.terminals = terminals_;
  s.coverage_fta = scalar_value("coverage.fta", 0.0);
  s.coverage_sta = scalar_value("coverage.sta", 0.0);
  s.stopped = stopped_;
  s.abandoned = abandoned_;
  return s;
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
    visited_actions_[state.action] = true;
    if (auto const & f = prepared.fta.actions[state.action].field) visited_fields_.insert(*f);
    tokens_total_ = perf_.text.tokens_eval + perf_.text.tokens_restore
                  + perf_.complete.tokens_eval + perf_.complete.tokens_restore
                  + perf_.choose.tokens_eval + perf_.choose.tokens_restore;

    // Early termination, checked once per action (never before a first
    // terminal exists, so a stopped run always has a complete path). Pending
    // subtrees are abandoned: their roots are marked pruned so the FTT stays
    // well-formed, distinguishable from threshold/width rejection.
    if (prepared.fta.queue_stop && terminals_ >= 1) {
      double const seconds_now = perf_.advance_seconds
          + std::chrono::duration<double>(clock::now() - advance_start).count();
      if (eval_stop(*prepared.fta.queue_stop, seconds_now)) {
        for (auto const & pending : queue) pending->parent.pruned = data::Pruned::Abandoned;
        abandoned_ = static_cast<unsigned>(queue.size());
        queue.clear();
        stopped_ = true;
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
