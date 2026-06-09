
#include "autocog/backend/llama/model.hxx"
#include "autocog/logging.hxx"

#include "autocog/data/vocab.hxx"

#include <regex>

#include <llama.h>

#include <cmath>
#include <algorithm>
#include <fstream>
#include <iterator>
#include "picosha2.h"
#include "autocog/utilities/exception.hxx"



namespace autocog::backend::llama {

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
  // Load model
  llama_model_params model_params = llama_model_default_params();
  this->model = llama_model_load_from_file(model_path.c_str(), model_params);
  if (!this->model) {
    throw autocog::ModelError("Failed to load model from: " + model_path, id, "load");
  }
   
  // Create context parameters
  llama_context_params ctx_params = llama_context_default_params();
  ctx_params.n_ctx = n_ctx;
   
  // Create single context with ID=0 (and associated token sequence)
  llama_context * ctx = llama_init_from_model(this->model, ctx_params);
  if (!ctx) {
    llama_model_free(this->model);
    throw autocog::ModelError("Failed to create llama context", id, "context");
  }
  this->contexts.push_back(ctx);
  this->tokens.emplace_back(); 
}

Model::~Model() {
  if (this->id == 0) {
    // NOP
  } else {
    // for (auto* ctx : this->contexts) if (ctx) llama_free(ctx);
    contexts.clear();
    if (this->model) llama_model_free(model);
  }
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

static float logit_to_log_sum_exp(float * logit, unsigned vocab_size) {
  // Find max logit for numerical stability
  float max_logit = *std::max_element(logit, logit + vocab_size);

  // Compute log-sum-exp for normalization
  float log_sum_exp = 0.0f;
  for (unsigned i = 0; i < vocab_size; ++i) {
    log_sum_exp += std::exp(logit[i] - max_logit);
  }
  return max_logit + std::log(log_sum_exp);
}

static void sample_logprobs(llama_context * ctx, std::vector<bool> const & mask, std::vector<std::pair<TokenID, float>> & candidates) {
  float * logits = llama_get_logits(ctx);
  float log_sum_exp = logit_to_log_sum_exp(logits, mask.size());

  for (unsigned tok = 0; tok < mask.size(); tok++) {
    if (mask[tok]) {
      candidates.emplace_back(tok, log_sum_exp - logits[tok]);
    }
  }
}

static float retrieve_logprob(llama_context * ctx, unsigned vocab_size, TokenID token) {
  float * logits = llama_get_logits(ctx);
  return logit_to_log_sum_exp(logits, vocab_size) - logits[token];
}

static llama_pos find_common_prefix(const TokenSequence& a, const TokenSequence& b) {
  llama_pos common = 0;
  size_t min_size = std::min(a.size(), b.size());
  while (static_cast<size_t>(common) < min_size && a[common] == b[common]) {
    common++;
  }
  return common;
}

unsigned Model::set_tokens(TokenSequence const & target_tokens, ContextID const id) {
  SPDLOG_LOGGER_TRACE(autocog::log(), "Model::set_tokens(...):");
  SPDLOG_LOGGER_TRACE(autocog::log(), " > target_tokens.size() =");
  if (this->id == 0) {
    this->tokens[id] = target_tokens;
    return target_tokens.size();
  }
  check_context_id(id);

  TokenSequence & current_tokens = this->get_tokens(id);
  SPDLOG_LOGGER_TRACE(autocog::log(), " > current_tokens.size() =");
  llama_context * ctx = this->get_context(id);
  unsigned n_ctx = llama_n_ctx(ctx);
  if (current_tokens.size() > n_ctx) {
    throw autocog::ModelError("Token sequence too long: " + std::to_string(current_tokens.size()) + " > " + std::to_string(n_ctx), id, "context_overflow");
  }

  llama_memory_t mem = llama_get_memory(ctx);
  SPDLOG_LOGGER_TRACE(autocog::log(), " > KV cache pos_min =");
  SPDLOG_LOGGER_TRACE(autocog::log(), " > KV cache pos_max =");

  llama_pos common_prefix = find_common_prefix(current_tokens, target_tokens);
  SPDLOG_LOGGER_TRACE(autocog::log(), " > common_prefix =");

  unsigned num_token_eval = 0;
  if (common_prefix == 0) {
    llama_memory_seq_rm(mem, 0, 0, -1);

    llama_batch batch = llama_batch_get_one(const_cast<TokenID*>(target_tokens.data()), target_tokens.size());
    if (llama_decode(ctx, batch) != 0) {
      throw autocog::ModelError("Failed to set the token sequence", id, "set_tokens");
    }
    num_token_eval += target_tokens.size();
  } else {
    if (static_cast<size_t>(common_prefix) < current_tokens.size()) {
      llama_memory_seq_rm(mem, 0, common_prefix, -1);
    }

    if (static_cast<size_t>(common_prefix) < target_tokens.size()) {
      TokenSequence extension(target_tokens.begin() + common_prefix, target_tokens.end());

      llama_batch batch = llama_batch_get_one(const_cast<TokenID*>(extension.data()), extension.size());
      
      std::vector<llama_pos> positions(extension.size());
      for (size_t i = 0; i < extension.size(); ++i) {
        positions[i] = common_prefix + i;
      }
      batch.pos = positions.data();

      if (llama_decode(ctx, batch) != 0) {
        throw autocog::ModelError("Failed to decode token", id, "decode");
      }
      num_token_eval += extension.size();
    }
  }
  current_tokens = target_tokens;
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

  TokenSequence & loc_tokens = this->get_tokens(id);
  llama_pos token_pos = loc_tokens.size();
  logprobs.clear();

  for (auto token: new_tokens) {
  SPDLOG_LOGGER_TRACE(autocog::log(), " > token_pos =");

    llama_batch batch = llama_batch_get_one(&token, 1);
    batch.pos = &token_pos;

    if (llama_decode(this->get_context(id), batch) != 0) {
      throw autocog::ModelError("Failed to decode token", id, "decode");
    }

    logprobs.push_back(retrieve_logprob(this->get_context(id), this->vocab_size(), token));

    token_pos++;
  }
  loc_tokens.insert(loc_tokens.end(), new_tokens.begin(), new_tokens.end());
  return new_tokens.size();
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

  llama_context * ctx = this->get_context(id);

  topk_tokens.clear();
  topk_lobprobs.clear();

  std::vector<std::pair<TokenID, float>> candidates;
  sample_logprobs(ctx, vocab_mask, candidates);

  // Handle edge case: no valid candidates
  if (candidates.empty()) {
    throw autocog::ModelError("Failed to find candidate token: empty vocabulary mask", id, "vocab_mask");
  }
    
  std::sort(candidates.begin(), candidates.end(), [](const auto& a, const auto& b) {
    return a.second < b.second;
  });
    
  size_t k = std::min(max_candidates, candidates.size());
  topk_tokens.reserve(k);
  topk_lobprobs.reserve(k);
    
  for (size_t i = 0; i < k; ++i) {
    topk_tokens.push_back(candidates[i].first);
    topk_lobprobs.push_back(candidates[i].second);
  }
    
  return 1;
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

