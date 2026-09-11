
#include "autocog/backend/llama/model.hxx"
#include "autocog/backend/llama/sampling.hxx"
#include "autocog/logging.hxx"

#include "autocog/data/vocab.hxx"

#include <regex>
#include <set>

#include <llama.h>

#include <chrono>
#include <cmath>
#include <cstdlib>
#include <algorithm>
#include <fstream>
#include <iterator>
#include "picosha2.h"
#include "autocog/utilities/exception.hxx"



namespace autocog::backend::llama {

// Number of KV slots (llama sequences) in the pool. More slots keep more
// branches resident across beam/choice ping-pong; the unified KV cache shares
// prefix cells between them, so the cost is only the divergent suffixes.
// The default covers a full beams=8 lookahead wave (8 beams x 8 candidates):
// measured on Llama-3.2-1B, beams=8/ahead=2 drops from 20410 to 269 restored
// tokens going from 16 to 64 slots. Overridable for experiments
// (AUTOCOG_KV_SLOTS=1 reproduces the historical single-sequence behavior).
static size_t kv_slot_count() {
  size_t n = 64;
  if (char const * env = std::getenv("AUTOCOG_KV_SLOTS")) {
    long v = std::strtol(env, nullptr, 10);
    if (v >= 1 && v <= 256) n = static_cast<size_t>(v);  // LLAMA_MAX_SEQ = 256
  }
  return n;
}

Model::Model() :
  id(0),
  model(nullptr),
  contexts(),
  rng(42)
{
  contexts.push_back(nullptr);
  tokens.emplace_back();
}

Model::Model(ModelID const id_, std::string const & model_path, int n_ctx) :
  id(id_),
  model(nullptr),
  contexts(),
  rng(0),
  source_(model_path)
{
  // Load model. GPU offload is opt-in via AUTOCOG_NGL (number of layers to
  // offload; 99 = whole model) so CPU-only environments stay the default and
  // benchmarks state their hardware explicitly.
  llama_model_params model_params = llama_model_default_params();
  if (char const * env = std::getenv("AUTOCOG_NGL")) {
    long v = std::strtol(env, nullptr, 10);
    if (v > 0) model_params.n_gpu_layers = static_cast<int>(v);
  }
  this->model = llama_model_load_from_file(model_path.c_str(), model_params);
  if (!this->model) {
    throw autocog::ModelError("Failed to load model from: " + model_path, id, "load");
  }
   
  // Create context parameters. One llama context; the slot pool multiplexes
  // n_seq_max sequences inside it over a unified KV buffer, so sequences share
  // prefix cells and llama_memory_seq_cp is metadata-only. n_batch must admit
  // a full-context prompt decode in one llama_decode call.
  llama_context_params ctx_params = llama_context_default_params();
  ctx_params.n_ctx = n_ctx;
  ctx_params.n_batch = n_ctx;
  ctx_params.n_seq_max = kv_slot_count();
  ctx_params.kv_unified = true;

  // Create single context with ID=0 (and associated token sequence)
  llama_context * ctx = llama_init_from_model(this->model, ctx_params);
  if (!ctx) {
    llama_model_free(this->model);
    throw autocog::ModelError("Failed to create llama context", id, "context");
  }
  this->contexts.push_back(ctx);
  this->tokens.emplace_back();
  this->slots_.resize(kv_slot_count());
}

Model::~Model() {
  if (this->id == 0) {
    // NOP
  } else {
    for (auto* ctx : this->contexts) if (ctx) llama_free(ctx);
    contexts.clear();
    if (this->model) llama_model_free(model);
  }
}

Model::Model(Model && o) noexcept
  : id(o.id),
    model(o.model),
    contexts(std::move(o.contexts)),
    tokens(std::move(o.tokens)),
    rng(std::move(o.rng)),
    slots_(std::move(o.slots_)),
    active_slot_(o.active_slot_),
    live_logits_slot_(o.live_logits_slot_),
    slot_clock_(o.slot_clock_),
    kv_stats_(o.kv_stats_),
    decode_stats_(o.decode_stats_),
    source_(std::move(o.source_)),
    sha_cache_(std::move(o.sha_cache_)),
    vocab_mask_cache_(std::move(o.vocab_mask_cache_)),
    full_vocab_mask_(std::move(o.full_vocab_mask_))
{
  // Transfer ownership: leave the moved-from object owning nothing, so its
  // destructor frees neither the model nor any context. (std::move on a vector
  // leaves it empty in practice, but clear() makes the no-op teardown explicit.)
  o.model = nullptr;
  o.contexts.clear();
}

void Model::check_context_id(ContextID const id) const {
  if (this->contexts.size() != this->tokens.size()) {
    throw autocog::utilities::InternalError("Discrepancy between contexts and tokens vector size");
  }
  if (id >= this->contexts.size()) {
    throw autocog::utilities::InternalError("Invalid context ID: " + std::to_string(id));
  }
  if (this->contexts[id] == nullptr && this->id != 0) {
    throw autocog::utilities::InternalError("Missing context ID: " + std::to_string(id));
  }
}

llama_context * Model::get_context(ContextID const id) const {
  check_context_id(id);
  return this->contexts[id];
}

TokenSequence & Model::get_tokens(ContextID const id) {
  check_context_id(id);
  return this->tokens[id];
}

TokenSequence Model::tokenize(std::string const & text, bool add_bos, bool special) {
  if (this->id == 0) {
    (void)special;
    TokenSequence result;
    if (add_bos) result.push_back(RNG_BOS);
    for (unsigned char c : text) {
      result.push_back(static_cast<TokenID>(c));
    }
    return result;
  }
  std::vector<TokenID> tokens;
  tokens.resize(text.length() + (add_bos ? 1 : 0) + 1);  // Rough upper bound

  int n_tokens = llama_tokenize(
      this->get_vocab(),
      text.c_str(),
      text.length(),
      tokens.data(),
      tokens.size(),
      add_bos,
      special
  );

  // A negative return is the required token count (the upper-bound buffer was
  // too small, e.g. special-token expansion). Grow to fit and retry once.
  if (n_tokens < 0) {
    tokens.resize(static_cast<size_t>(-n_tokens));
    n_tokens = llama_tokenize(
        this->get_vocab(),
        text.c_str(),
        text.length(),
        tokens.data(),
        tokens.size(),
        add_bos,
        special
    );
  }

  if (n_tokens < 0) {
    throw autocog::ModelError("Tokenization failed for text: " + text, id, "tokenize");
  }

  TokenSequence result(tokens.begin(), tokens.begin() + n_tokens);
  return result;
}

std::string Model::detokenize(TokenSequence const & tokens, bool spec_rm, bool spec_unp) {
  if (this->id == 0) {
    (void)spec_unp;
    std::string result;
    for (auto tok : tokens) {
      if (spec_rm && (tok == RNG_BOS || tok == RNG_EOS)) continue;
      if (tok >= 0 && tok < 256) {
        result.push_back(static_cast<char>(tok));
      }
    }
    return result;
  }

  if (tokens.empty()) {
    return "";
  }

  // Detokenize
  std::string result;
  result.resize(tokens.size() * 8);  // Rough estimate for buffer size
   
  int n_chars = llama_detokenize(
      this->get_vocab(),
      tokens.data(),
      tokens.size(),
      &result[0],
      result.size(),
      spec_rm,
      spec_unp
  );

  // A negative return is the required buffer size (the estimate was too small,
  // e.g. long multi-byte tokens). Grow to fit and retry once.
  if (n_chars < 0) {
    result.resize(static_cast<size_t>(-n_chars));
    n_chars = llama_detokenize(
        this->get_vocab(),
        tokens.data(),
        tokens.size(),
        &result[0],
        result.size(),
        spec_rm,
        spec_unp
    );
  }

  if (n_chars < 0) {
    throw autocog::ModelError("Detokenization failed", id, "detokenize");
  }

  result.resize(n_chars);
  return result;
}

const llama_vocab * Model::get_vocab() const {
  if (this->id == 0) {
    throw autocog::NotImplementedError("Using model #0 (RNG) for this operation is not implemented");
  }
  return llama_model_get_vocab(this->model);
}

size_t Model::vocab_size() const {
  if (this->id == 0) {
    return RNG_VOCAB_SIZE;
  }
  return llama_vocab_n_tokens(this->get_vocab());
}

using sampling::log_sum_exp;
using sampling::row_logprob;
using sampling::topk_masked;

// Masked top-k candidates (ascending -log P) from one logits row.
static void topk_from_row(float const * row, std::vector<bool> const & mask,
                          size_t k, FrontierResult & out, ModelID model_id) {
  std::vector<std::pair<unsigned, float>> top;
  topk_masked(row, mask, k, top);
  if (top.empty()) {
    throw autocog::ModelError("Failed to find candidate token: empty vocabulary mask", model_id, "vocab_mask");
  }
  float const lse = log_sum_exp(row, mask.size());
  out.tokens.clear();
  out.logprobs.clear();
  out.tokens.reserve(top.size());
  out.logprobs.reserve(top.size());
  for (auto const & [tok, logit] : top) {
    out.tokens.push_back(static_cast<TokenID>(tok));
    out.logprobs.push_back(lse - logit);
  }
}

static llama_pos find_common_prefix(const TokenSequence& a, const TokenSequence& b) {
  llama_pos common = 0;
  size_t min_size = std::min(a.size(), b.size());
  while (static_cast<size_t>(common) < min_size && a[common] == b[common]) {
    common++;
  }
  return common;
}

size_t Model::pick_victim(size_t keep) const {
  size_t victim = keep;
  for (size_t s = 0; s < slots_.size(); ++s) {
    if (s == keep) continue;
    if (victim == keep || slots_[s].last_used < slots_[victim].last_used) victim = s;
  }
  return victim;
}

float const * Model::live_row(ContextID const id) const {
  return llama_get_logits_ith(this->get_context(id), live_logits_ith_);
}

unsigned Model::decode_extension(size_t slot, int pos0, TokenID const * toks, size_t n,
                                 ContextID const id, bool all_logits) {
  if (n == 0) return 0;
  llama_context * ctx = this->get_context(id);
  llama_memory_t mem = llama_get_memory(ctx);

  llama_batch batch = llama_batch_init(n, 0, 1);
  batch.n_tokens = static_cast<int32_t>(n);
  for (size_t i = 0; i < n; ++i) {
    batch.token[i] = toks[i];
    batch.pos[i] = pos0 + static_cast<int>(i);
    batch.n_seq_id[i] = 1;
    batch.seq_id[i][0] = static_cast<llama_seq_id>(slot);
    batch.logits[i] = all_logits || (i + 1 == n);
  }

  auto const t_dec = std::chrono::steady_clock::now();
  int ret = llama_decode(ctx, batch);
  ++decode_stats_.calls;
  if (ret != 0) {
    // KV cells exhausted (divergent suffixes accumulate across slots): drop
    // every other slot, discard any partial insertion, and retry once.
    for (size_t s = 0; s < slots_.size(); ++s) {
      if (s == slot || slots_[s].tokens.empty()) continue;
      llama_memory_seq_rm(mem, static_cast<llama_seq_id>(s), -1, -1);
      slots_[s].tokens.clear();
      ++kv_stats_.evictions;
    }
    llama_memory_seq_rm(mem, static_cast<llama_seq_id>(slot), pos0, -1);
    ret = llama_decode(ctx, batch);
    ++decode_stats_.calls;
  }
  decode_stats_.decode_seconds +=
      std::chrono::duration<double>(std::chrono::steady_clock::now() - t_dec).count();
  llama_batch_free(batch);
  if (ret != 0) {
    throw autocog::ModelError("Failed to decode tokens", this->id, "decode");
  }
  live_logits_slot_ = static_cast<int>(slot);
  live_logits_ith_ = static_cast<int>(n) - 1;
  return n;
}

unsigned Model::set_tokens(TokenSequence const & target_tokens, ContextID const id,
                           bool prime_logits) {
  SPDLOG_LOGGER_TRACE(autocog::log(), "Model::set_tokens(...):");
  SPDLOG_LOGGER_TRACE(autocog::log(), " > target_tokens.size() =");
  if (this->id == 0) {
    this->tokens[id] = target_tokens;
    return target_tokens.size();
  }
  check_context_id(id);

  llama_context * ctx = this->get_context(id);
  unsigned n_ctx = llama_n_ctx(ctx);
  if (target_tokens.size() > n_ctx) {
    throw autocog::ModelError("Token sequence too long: " + std::to_string(target_tokens.size()) + " > " + std::to_string(n_ctx), id, "context_overflow");
  }

  llama_memory_t mem = llama_get_memory(ctx);
  ++slot_clock_;

  // Route to the slot with the longest common prefix; among ties prefer one
  // the target extends in place (no fork), then the most recently used.
  size_t best = 0;
  size_t best_cp = 0;
  for (size_t s = 0; s < slots_.size(); ++s) {
    size_t cp = find_common_prefix(slots_[s].tokens, target_tokens);
    if (s == 0 || cp > best_cp
        || (cp == best_cp && slots_[s].tokens.size() == cp && slots_[best].tokens.size() != best_cp)
        || (cp == best_cp && (slots_[s].tokens.size() == cp) == (slots_[best].tokens.size() == best_cp)
            && slots_[s].last_used > slots_[best].last_used)) {
      best = s;
      best_cp = cp;
    }
  }
  SPDLOG_LOGGER_TRACE(autocog::log(), " > common_prefix =");

  unsigned num_token_eval = 0;
  if (best_cp == target_tokens.size()) {
    // Fully cached. A longer slot is trimmed in place: its tail (a rolled-out
    // or superseded continuation) is never needed once we return here.
    if (slots_[best].tokens.size() > best_cp) {
      llama_memory_seq_rm(mem, static_cast<llama_seq_id>(best), static_cast<llama_pos>(best_cp), -1);
      slots_[best].tokens.resize(best_cp);
      if (live_logits_slot_ == static_cast<int>(best)) live_logits_slot_ = -1;
      ++kv_stats_.trims;
    } else {
      ++kv_stats_.exact;
    }
    active_slot_ = best;
  } else if (best_cp == slots_[best].tokens.size()) {
    // The slot (possibly empty) is a strict prefix: append in place.
    num_token_eval += decode_extension(best, static_cast<int>(best_cp),
                                       target_tokens.data() + best_cp,
                                       target_tokens.size() - best_cp, id);
    slots_[best].tokens = target_tokens;
    ++kv_stats_.extends;
    active_slot_ = best;
  } else {
    // Divergent target: fork. Keep the source resident (a sibling branch will
    // come back to it), copy the shared prefix into an LRU victim — metadata
    // only under kv_unified — and decode just the divergent suffix.
    size_t victim = pick_victim(best);
    if (victim == best) {
      // Single-slot pool: legacy behavior, trim the divergence point and redecode.
      llama_memory_seq_rm(mem, static_cast<llama_seq_id>(best), static_cast<llama_pos>(best_cp), -1);
      slots_[best].tokens.resize(best_cp);
      if (live_logits_slot_ == static_cast<int>(best)) live_logits_slot_ = -1;
      victim = best;
    } else {
      if (!slots_[victim].tokens.empty()) {
        llama_memory_seq_rm(mem, static_cast<llama_seq_id>(victim), -1, -1);
        slots_[victim].tokens.clear();
        ++kv_stats_.evictions;
      }
      if (live_logits_slot_ == static_cast<int>(victim)) live_logits_slot_ = -1;
      if (best_cp > 0)
        llama_memory_seq_cp(mem, static_cast<llama_seq_id>(best), static_cast<llama_seq_id>(victim),
                            0, static_cast<llama_pos>(best_cp));
      slots_[victim].tokens.assign(target_tokens.begin(), target_tokens.begin() + best_cp);
    }
    num_token_eval += decode_extension(victim, static_cast<int>(best_cp),
                                       target_tokens.data() + best_cp,
                                       target_tokens.size() - best_cp, id);
    slots_[victim].tokens = target_tokens;
    ++kv_stats_.forks;
    active_slot_ = victim;
  }
  slots_[active_slot_].last_used = slot_clock_;

  // The zero-decode paths leave the live logits belonging to some other
  // sequence; re-decode the final token in place to regenerate them.
  if (prime_logits && live_logits_slot_ != static_cast<int>(active_slot_)
      && !target_tokens.empty()) {
    llama_pos last = static_cast<llama_pos>(target_tokens.size()) - 1;
    llama_memory_seq_rm(mem, static_cast<llama_seq_id>(active_slot_), last, -1);
    num_token_eval += decode_extension(active_slot_, last, &target_tokens.back(), 1, id);
    ++kv_stats_.tokens_primed;
  }
  return num_token_eval;
}

unsigned Model::eval_sequences(TokenSequence const & new_tokens, ProbaSequence & logprobs, ContextID const id) {
  SPDLOG_LOGGER_TRACE(autocog::log(), "Model::eval_sequences(...):");
  SPDLOG_LOGGER_TRACE(autocog::log(), " > new_tokens.size() =");
  if (this->id == 0) {
    std::exponential_distribution<float> dist(0.5f);
    logprobs.clear();
    for (size_t i = 0; i < new_tokens.size(); ++i) {
      logprobs.push_back(dist(this->rng));
    }
    this->tokens[id].insert(this->tokens[id].end(), new_tokens.begin(), new_tokens.end());
    return new_tokens.size();
  }

  check_context_id(id);
  // Forced scoring: P(token_i | prefix, token_<i). The distribution for each
  // token is the one produced *before* it is decoded — the prefix's primed
  // final-position logits for the first token, then the previous position's
  // row of a whole-sequence decode with per-position logits. Chunked so the
  // per-position logits buffer (chunk × vocab floats) stays bounded.
  if (live_logits_slot_ != static_cast<int>(active_slot_)) {
    throw autocog::utilities::InternalError("eval_sequences: live logits do not belong to the active KV slot (missing prime_logits on set_tokens?)");
  }
  Slot & slot = this->slots_[active_slot_];
  int const pos0 = static_cast<int>(slot.tokens.size());
  size_t const n = new_tokens.size();
  unsigned const vocab = this->vocab_size();
  llama_context * ctx = this->get_context(id);
  logprobs.clear();
  if (n == 0) return 0;

  size_t constexpr CHUNK = 256;
  auto t_smp = std::chrono::steady_clock::now();
  logprobs.push_back(row_logprob(live_row(id), vocab, new_tokens[0]));
  decode_stats_.score_seconds +=
      std::chrono::duration<double>(std::chrono::steady_clock::now() - t_smp).count();
  for (size_t done = 0; done < n; done += CHUNK) {
    size_t const len = std::min(CHUNK, n - done);
    decode_extension(active_slot_, pos0 + static_cast<int>(done),
                     new_tokens.data() + done, len, id, /*all_logits=*/true);
    t_smp = std::chrono::steady_clock::now();
    for (size_t j = 0; j < len; ++j) {
      size_t const scored = done + j + 1;  // row j predicts the next token
      if (scored < n)
        logprobs.push_back(row_logprob(llama_get_logits_ith(ctx, static_cast<int>(j)),
                                       vocab, new_tokens[scored]));
    }
    decode_stats_.score_seconds +=
        std::chrono::duration<double>(std::chrono::steady_clock::now() - t_smp).count();
  }
  slot.tokens.insert(slot.tokens.end(), new_tokens.begin(), new_tokens.end());
  slot.last_used = ++slot_clock_;
  return n;
}


unsigned Model::eval_topk_tokens(
  std::vector<bool> const & vocab_mask,
  size_t max_candidates,
  std::vector<TokenID> & topk_tokens,
  std::vector<float> & topk_lobprobs,
  ContextID const id
) {
  SPDLOG_LOGGER_TRACE(autocog::log(), "Model::eval_topk_tokens(...):");
  SPDLOG_LOGGER_TRACE(autocog::log(), " > max_candidates =");

  check_context_id(id);
  if (this->id == 0) {
    size_t vs = RNG_VOCAB_SIZE;
    if (vocab_mask.size() != vs) {
      throw autocog::ModelError("vocab_mask size mismatch: " + std::to_string(vocab_mask.size()) + " vs " + std::to_string(vs), id, "vocab_mask");
    }

    // RNG model: exponential logprobs (λ=0.5, mean=2)
    // Produces realistic peaked distribution: clear winner, long tail.
    // Pruning thresholds work naturally (top 2-3 candidates survive).
    std::exponential_distribution<float> dist(0.5f);
    std::vector<std::pair<TokenID, float>> candidates;
    for (size_t tok = 0; tok < vs; ++tok) {
      if (vocab_mask[tok]) {
        // Restrict to printable ASCII (32-126), newline, tab
        if (tok < 256 && !(tok >= 32 && tok <= 126) && tok != '\n' && tok != '\t') continue;
        candidates.emplace_back(static_cast<TokenID>(tok), dist(this->rng));
      }
    }

    if (candidates.empty()) {
      throw autocog::ModelError("Failed to find candidate token: empty vocabulary mask", id, "vocab_mask");
    }

    std::sort(candidates.begin(), candidates.end(), [](const auto& a, const auto& b) {
      return a.second < b.second;
    });

    size_t k = std::min(max_candidates, candidates.size());
    topk_tokens.clear();
    topk_lobprobs.clear();
    topk_tokens.reserve(k);
    topk_lobprobs.reserve(k);
    for (size_t i = 0; i < k; ++i) {
      topk_tokens.push_back(candidates[i].first);
      topk_lobprobs.push_back(candidates[i].second);
    }
    return 1;
  }

  size_t vocab_size = this->vocab_size();
  if (vocab_mask.size() != vocab_size) {
     throw autocog::ModelError("vocab_mask size mismatch: " + std::to_string(vocab_mask.size()) + " vs " + std::to_string(vocab_size), id, "vocab_mask");
  }

  // The live logits must be those of the active slot's final position; callers
  // guarantee it via set_tokens(..., prime_logits=true) or a preceding decode.
  // Anything else would silently sample from a sibling branch's distribution.
  if (live_logits_slot_ != static_cast<int>(active_slot_)) {
    throw autocog::utilities::InternalError("eval_topk_tokens: live logits do not belong to the active KV slot (missing prime_logits on set_tokens?)");
  }

  topk_tokens.clear();
  topk_lobprobs.clear();

  auto const t_smp = std::chrono::steady_clock::now();
  FrontierResult top;
  topk_from_row(live_row(id), vocab_mask, max_candidates, top, this->id);
  topk_tokens = std::move(top.tokens);
  topk_lobprobs = std::move(top.logprobs);
  decode_stats_.sample_seconds +=
      std::chrono::duration<double>(std::chrono::steady_clock::now() - t_smp).count();

  return 1;
}

unsigned Model::topk_frontier(
  std::vector<TokenSequence> const & targets,
  std::vector<bool> const & vocab_mask,
  size_t max_candidates,
  std::vector<FrontierResult> & results,
  ContextID const id
) {
  check_context_id(id);
  results.assign(targets.size(), FrontierResult{});
  if (targets.empty()) return 0;

  if (this->id == 0) {
    // RNG model: sequential, preserving the historical per-beam draw order.
    // The returned count mirrors the legacy accounting (a full target restore
    // plus one scored token each).
    unsigned decoded = 0;
    for (size_t t = 0; t < targets.size(); ++t) {
      decoded += set_tokens(targets[t], id);
      eval_topk_tokens(vocab_mask, max_candidates, results[t].tokens, results[t].logprobs, id);
      decoded += 1;
    }
    return decoded;
  }

  size_t const vocab = vocab_size();
  if (vocab_mask.size() != vocab) {
    throw autocog::ModelError("vocab_mask size mismatch: " + std::to_string(vocab_mask.size()) + " vs " + std::to_string(vocab), id, "vocab_mask");
  }
  llama_context * ctx = get_context(id);
  llama_memory_t mem = llama_get_memory(ctx);
  unsigned const n_ctx = llama_n_ctx(ctx);

  struct Item { TokenID token; int pos; size_t slot; bool want_row; };
  struct Planned { size_t target; size_t slot; int final_idx; };

  unsigned decoded_total = 0;
  size_t t = 0;
  while (t < targets.size()) {
    ++slot_clock_;
    std::vector<Item> items;
    std::vector<Planned> planned;
    std::set<size_t> pinned;                 // slots carrying batch-pending tokens
    std::map<size_t, size_t> pending_from;   // slot -> first not-yet-decoded position

    auto materialized = [&](size_t s) -> size_t {
      auto it = pending_from.find(s);
      return it == pending_from.end() ? slots_[s].tokens.size() : it->second;
    };
    auto push_extension = [&](size_t slot, size_t from, TokenSequence const & target) {
      for (size_t p = from; p < target.size(); ++p)
        items.push_back({target[p], static_cast<int>(p), slot, p + 1 == target.size()});
    };

    size_t const batch_start = t;
    for (; t < targets.size(); ++t) {
      TokenSequence const & target = targets[t];
      if (target.empty())
        throw autocog::utilities::InternalError("topk_frontier: empty target");
      if (target.size() > n_ctx)
        throw autocog::ModelError("Token sequence too long: " + std::to_string(target.size()) + " > " + std::to_string(n_ctx), id, "context_overflow");

      // Route like set_tokens. Fork/copy points must land on already-decoded
      // cells, so each slot's usable common prefix is clamped to what is
      // materialized (pending batch tokens have no KV cells yet).
      size_t best = 0, best_cp = 0;
      for (size_t s = 0; s < slots_.size(); ++s) {
        size_t cp = std::min<size_t>(find_common_prefix(slots_[s].tokens, target), materialized(s));
        bool const s_ext = (cp == slots_[s].tokens.size());
        bool const b_ext = (best_cp == slots_[best].tokens.size());
        if (s == 0 || cp > best_cp
            || (cp == best_cp && s_ext && !b_ext)
            || (cp == best_cp && s_ext == b_ext && slots_[s].last_used > slots_[best].last_used)) {
          best = s;
          best_cp = cp;
        }
      }

      size_t dest;
      if (best_cp == target.size() && !pinned.count(best)) {
        // Fully cached in an unpinned slot: reuse in place; the final token is
        // re-decoded to produce this target's logits row.
        if (slots_[best].tokens.size() > best_cp) ++kv_stats_.trims; else ++kv_stats_.exact;
        llama_memory_seq_rm(mem, static_cast<llama_seq_id>(best), static_cast<llama_pos>(best_cp - 1), -1);
        slots_[best].tokens = target;
        ++kv_stats_.tokens_primed;
        pending_from[best] = best_cp - 1;
        items.push_back({target.back(), static_cast<int>(best_cp - 1), best, true});
        dest = best;
      } else if (best_cp == slots_[best].tokens.size() && best_cp < target.size()) {
        // Strict-prefix slot: contiguous append. (A pinned slot's record
        // already includes its pending tokens, so appending after them keeps
        // batch positions contiguous — causal attention within one decode
        // covers the pending prefix.)
        push_extension(best, best_cp, target);
        if (!pending_from.count(best)) pending_from[best] = best_cp;
        slots_[best].tokens = target;
        ++kv_stats_.extends;
        dest = best;
      } else {
        // Divergent target (or fully cached in a pinned slot): fork the
        // usable prefix into an unpinned LRU victim and decode the rest.
        size_t victim = slots_.size();
        for (size_t s = 0; s < slots_.size(); ++s) {
          if (s == best || pinned.count(s)) continue;
          if (victim == slots_.size() || slots_[s].last_used < slots_[victim].last_used) victim = s;
        }
        if (victim == slots_.size()) {
          if (!planned.empty()) break;  // out of unpinned slots: flush, continue
          // Single-slot pool, nothing planned: legacy in-place trim+redecode.
          llama_memory_seq_rm(mem, static_cast<llama_seq_id>(best), static_cast<llama_pos>(best_cp), -1);
          slots_[best].tokens.resize(best_cp);
          push_extension(best, best_cp, target);
          pending_from[best] = best_cp;
          slots_[best].tokens = target;
          ++kv_stats_.trims;
          dest = best;
        } else {
          if (!slots_[victim].tokens.empty()) {
            llama_memory_seq_rm(mem, static_cast<llama_seq_id>(victim), -1, -1);
            ++kv_stats_.evictions;
          }
          // Copy only cells the fork can use; a fully-cached target still
          // re-decodes its final token for the logits row.
          size_t const copy_to = std::min(best_cp, target.size() - 1);
          if (copy_to > 0)
            llama_memory_seq_cp(mem, static_cast<llama_seq_id>(best), static_cast<llama_seq_id>(victim),
                                0, static_cast<llama_pos>(copy_to));
          push_extension(victim, copy_to, target);
          pending_from[victim] = copy_to;
          slots_[victim].tokens = target;
          ++kv_stats_.forks;
          if (best_cp == target.size()) ++kv_stats_.tokens_primed;
          dest = victim;
        }
      }
      pinned.insert(dest);
      slots_[dest].last_used = slot_clock_;
      planned.push_back({t, dest, static_cast<int>(items.size()) - 1});
    }

    if (t == batch_start) {
      throw autocog::utilities::InternalError("topk_frontier: no progress");
    }

    // One decode for everything planned; per-target rows at the final tokens.
    llama_batch batch = llama_batch_init(items.size(), 0, 1);
    batch.n_tokens = static_cast<int32_t>(items.size());
    for (size_t i = 0; i < items.size(); ++i) {
      batch.token[i] = items[i].token;
      batch.pos[i] = items[i].pos;
      batch.n_seq_id[i] = 1;
      batch.seq_id[i][0] = static_cast<llama_seq_id>(items[i].slot);
      batch.logits[i] = items[i].want_row;
    }
    auto const t_dec = std::chrono::steady_clock::now();
    int ret = llama_decode(ctx, batch);
    ++decode_stats_.calls;
    if (ret != 0) {
      // KV cells exhausted: drop unpinned slots, discard any partial
      // insertion of the pending ranges, and retry once.
      for (size_t s = 0; s < slots_.size(); ++s) {
        if (pinned.count(s) || slots_[s].tokens.empty()) continue;
        llama_memory_seq_rm(mem, static_cast<llama_seq_id>(s), -1, -1);
        slots_[s].tokens.clear();
        ++kv_stats_.evictions;
      }
      for (auto const & [s, from] : pending_from)
        llama_memory_seq_rm(mem, static_cast<llama_seq_id>(s), static_cast<llama_pos>(from), -1);
      ret = llama_decode(ctx, batch);
      ++decode_stats_.calls;
    }
    decode_stats_.decode_seconds +=
        std::chrono::duration<double>(std::chrono::steady_clock::now() - t_dec).count();
    llama_batch_free(batch);
    if (ret != 0) {
      throw autocog::ModelError("Failed to decode tokens", this->id, "decode");
    }
    decoded_total += static_cast<unsigned>(items.size());

    auto const t_smp = std::chrono::steady_clock::now();
    for (Planned const & p : planned) {
      topk_from_row(llama_get_logits_ith(ctx, p.final_idx), vocab_mask,
                    max_candidates, results[p.target], this->id);
    }
    decode_stats_.sample_seconds +=
        std::chrono::duration<double>(std::chrono::steady_clock::now() - t_smp).count();
    live_logits_slot_ = static_cast<int>(planned.back().slot);
    live_logits_ith_ = planned.back().final_idx;
  }
  return decoded_total;
}

std::string Model::sha256() const {
  // The RNG model has no backing file; report a stable sentinel so an FTT it
  // produced still records which "model" evaluated it.
  if (source_.empty()) return "rng";
  if (sha_cache_.empty()) {
    std::ifstream f(source_, std::ios::binary);
    if (!f)
      throw autocog::ModelError("Cannot open model file to hash: " + source_, id, "hash");
    // Stream the file through SHA-256 (full 64-hex) so a multi-GB GGUF is not
    // loaded into memory. Cached for the lifetime of the loaded model.
    sha_cache_ = picosha2::hash256_hex_string(
        std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
  }
  return sha_cache_;
}

std::vector<bool> Model::build_vocab_mask(autocog::data::VocabExpr const & ve) {
  size_t const n = vocab_size();
  using Kind = autocog::data::VocabExpr::Kind;
  switch (ve.kind) {
    case Kind::Tokenize: {
      std::vector<bool> m(n, false);
      for (auto const & s : ve.strings)
        for (TokenID t : tokenize(s, false, true))
          if (t >= 0 && static_cast<size_t>(t) < n) m[t] = true;
      return m;
    }
    case Kind::Regex: {
      std::vector<bool> m(n, false);
      std::regex re(ve.strings.empty() ? std::string{} : ve.strings[0]);
      for (size_t t = 0; t < n; ++t) {
        std::string surface = detokenize({static_cast<TokenID>(t)}, false, false);
        if (std::regex_search(surface, re)) m[t] = true;
      }
      return m;
    }
    case Kind::Union: {
      auto a = build_vocab_mask(ve.operands[0]);
      auto b = build_vocab_mask(ve.operands[1]);
      for (size_t i = 0; i < n; ++i) a[i] = a[i] || b[i];
      return a;
    }
    case Kind::Intersect: {
      auto a = build_vocab_mask(ve.operands[0]);
      auto b = build_vocab_mask(ve.operands[1]);
      for (size_t i = 0; i < n; ++i) a[i] = a[i] && b[i];
      return a;
    }
    case Kind::Diff: {
      auto a = build_vocab_mask(ve.operands[0]);
      auto b = build_vocab_mask(ve.operands[1]);
      for (size_t i = 0; i < n; ++i) a[i] = a[i] && !b[i];
      return a;
    }
    case Kind::Complement: {
      auto a = build_vocab_mask(ve.operands[0]);
      a.flip();
      return a;
    }
  }
  return std::vector<bool>(n, true);
}

std::vector<bool> const & Model::vocab_mask(std::string const & ref, autocog::data::VocabExpr const & expr) {
  auto it = vocab_mask_cache_.find(ref);
  if (it != vocab_mask_cache_.end()) return it->second;
  return vocab_mask_cache_.emplace(ref, build_vocab_mask(expr)).first->second;
}

std::vector<bool> const & Model::full_vocab_mask() {
  if (full_vocab_mask_.size() != vocab_size())
    full_vocab_mask_.assign(vocab_size(), true);
  return full_vocab_mask_;
}

}

