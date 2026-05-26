
#include "autocog/llama/xfta/model.hxx"

#include <llama.h>

#include <cmath>
#include <algorithm>
#include <stdexcept>

#if VERBOSE
#  include <iostream>
#endif

#define DEBUG_Model_set_tokens VERBOSE && 0
#define DEBUG_Model_eval_sequences VERBOSE && 0
#define DEBUG_Model_eval_topk_tokens VERBOSE && 0

namespace autocog::llama::xfta {

Model::Model() :
  id(0),
  model(nullptr),
  contexts()
{}

Model::Model(ModelID const id_, std::string const & model_path, int n_ctx) :
  id(id_),
  model(nullptr),
  contexts()
{
  // Load model
  llama_model_params model_params = llama_model_default_params();
  this->model = llama_model_load_from_file(model_path.c_str(), model_params);
  if (!this->model) {
    throw std::runtime_error("Failed to load model from: " + model_path);
  }
   
  // Create context parameters
  llama_context_params ctx_params = llama_context_default_params();
  ctx_params.n_ctx = n_ctx;
   
  // Create single context with ID=0 (and associated token sequence)
  llama_context * ctx = llama_init_from_model(this->model, ctx_params);
  if (!ctx) {
    llama_model_free(this->model);
    throw std::runtime_error("Failed to create llama context");
  }
  this->contexts.push_back(ctx);
  this->tokens.emplace_back(); 
}

Model::~Model() {
#if VERBOSE
  std::cerr << "Model::~Model(" << this->id << ")" << std::endl;
#endif
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
    throw std::runtime_error("Discrepency between contexts and tokens vector size.");
  }
  if (id >= this->contexts.size()) {
    throw std::runtime_error("Invalid context ID: " + std::to_string(id));
  }
  if (this->contexts[id] == nullptr) {
    throw std::runtime_error("Missing context ID: " + std::to_string(id));
  }
}

llama_context * Model::get_context(ContextID const id) const {
  check_context_id(id);
  return this->contexts[id];
}

TokenSequence const & Model::get_tokens_const(ContextID const id) const {
  check_context_id(id);
  return this->tokens[id];
}

TokenSequence & Model::get_tokens(ContextID const id) {
  check_context_id(id);
  return this->tokens[id];
}

ContextID Model::fork_context(ContextID const) {
  throw std::runtime_error("Context forking is not implemented yet (Phase 3 feature)");
}

TokenSequence Model::tokenize(std::string const & text, bool add_bos, bool special) {
  if (this->id == 0) {
    throw std::runtime_error("Using model #0 (RNG) is not implemented yet!");
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
    throw std::runtime_error("Tokenization failed for text: " + text);
  }
   
  TokenSequence result(tokens.begin(), tokens.begin() + n_tokens);
  return result;
}

std::string Model::detokenize(TokenSequence const & tokens, bool spec_rm, bool spec_unp) {
  if (this->id == 0) {
    throw std::runtime_error("Using model #0 (RNG) is not implemented yet!");
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
    throw std::runtime_error("Detokenization failed");
  }
   
  result.resize(n_chars);
  return result;
}

const llama_vocab * Model::get_vocab() const {
  if (this->id == 0) {
    throw std::runtime_error("Using model #0 (RNG) is not implemented yet!");
  }
  return llama_model_get_vocab(this->model);
}

size_t Model::vocab_size() const {
  if (this->id == 0) {
    throw std::runtime_error("Using model #0 (RNG) is not implemented yet!");
  }
  return llama_vocab_n_tokens(this->get_vocab());
}

TokenID Model::bos_token() const {
  if (this->id == 0) {
    throw std::runtime_error("Using model #0 (RNG) is not implemented yet!");
  }
  return llama_vocab_bos(this->get_vocab());
}

TokenID Model::eos_token() const {
  if (this->id == 0) {
    throw std::runtime_error("Using model #0 (RNG) is not implemented yet!");
  }
  return llama_vocab_eos(this->get_vocab());
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

static void retrieve_logprobs(llama_context * ctx, unsigned vocab_size, std::vector<float> & logprobs) {
  float * logits = llama_get_logits(ctx);
  float log_sum_exp = logit_to_log_sum_exp(logits, vocab_size);
  logprobs.resize(vocab_size);
  for (unsigned tok = 0; tok < vocab_size; tok++) {
    logprobs[tok] = log_sum_exp - logits[tok];
  }
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
  while (common < min_size && a[common] == b[common]) {
    common++;
  }
  return common;
}

unsigned Model::set_tokens(TokenSequence const & target_tokens, ContextID const id) {
#if DEBUG_Model_set_tokens
  std::cerr << "Model::set_tokens(...):" << std::endl;
  std::cerr << " > target_tokens.size() = " << target_tokens.size() << std::endl;
#endif
  if (this->id == 0) {
    return target_tokens.size();
  }
  check_context_id(id);

  TokenSequence & current_tokens = this->get_tokens(id);
#if DEBUG_Model_set_tokens
  std::cerr << " > current_tokens.size() = " << current_tokens.size() << std::endl;
#endif
  llama_context * ctx = this->get_context(id);
  unsigned n_ctx = llama_n_ctx(ctx);
  if (current_tokens.size() > n_ctx) {
    throw std::runtime_error("Token sequence too long: " + std::to_string(current_tokens.size()) + " > " + std::to_string(n_ctx));
  }

  llama_memory_t mem = llama_get_memory(ctx);
#if DEBUG_Model_set_tokens
  llama_pos kv_pos_max = llama_memory_seq_pos_max(mem, 0);
  llama_pos kv_pos_min = llama_memory_seq_pos_min(mem, 0);
  std::cerr << " > KV cache pos_min = " << kv_pos_min << std::endl;
  std::cerr << " > KV cache pos_max = " << kv_pos_max << std::endl;
#endif

  llama_pos common_prefix = find_common_prefix(current_tokens, target_tokens);
#if DEBUG_Model_set_tokens
  std::cerr << " > common_prefix = " << common_prefix << std::endl;
#endif

  unsigned num_token_eval = 0;
  if (common_prefix == 0) {
    llama_memory_seq_rm(mem, 0, 0, -1);

    llama_batch batch = llama_batch_get_one(const_cast<TokenID*>(target_tokens.data()), target_tokens.size());
    if (llama_decode(ctx, batch) != 0) {
      throw std::runtime_error("Failed to set the token sequence.");
    }
    num_token_eval += target_tokens.size();
  } else {
    if (common_prefix < current_tokens.size()) {
      llama_memory_seq_rm(mem, 0, common_prefix, -1);
    }

    if (common_prefix < target_tokens.size()) {
      TokenSequence extension(target_tokens.begin() + common_prefix, target_tokens.end());

      llama_batch batch = llama_batch_get_one(const_cast<TokenID*>(extension.data()), extension.size());
      
      std::vector<llama_pos> positions(extension.size());
      for (size_t i = 0; i < extension.size(); ++i) {
        positions[i] = common_prefix + i;
      }
      batch.pos = positions.data();

      if (llama_decode(ctx, batch) != 0) {
        throw std::runtime_error("Failed to decode token");
      }
      num_token_eval += extension.size();
    }
  }
  current_tokens = target_tokens;
  return num_token_eval;
}

unsigned Model::eval_sequences(TokenSequence const & new_tokens, ProbaSequence & logprobs, ContextID const id) {
#if DEBUG_Model_eval_sequences
  std::cerr << "Model::eval_sequences(...):" << std::endl;
  std::cerr << " > new_tokens.size() = " << new_tokens.size() << std::endl;
#endif
  if (this->id == 0) {
    throw std::runtime_error("Using model #0 (RNG) is not implemented yet!");
    // TODO fill `logprobs` with random vales
    return new_tokens.size();
  }

  TokenSequence & loc_tokens = this->get_tokens(id);
  llama_pos token_pos = loc_tokens.size();
  logprobs.clear();

  for (auto token: new_tokens) {
#if DEBUG_Model_set_tokens
    std::cerr << " > token_pos = " << token_pos << std::endl;
#endif

    llama_batch batch = llama_batch_get_one(&token, 1);
    batch.pos = &token_pos;

    if (llama_decode(this->get_context(id), batch) != 0) {
      throw std::runtime_error("Failed to decode token");
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
#if DEBUG_Model_eval_topk_tokens
  std::cerr << "Model::eval_topk_tokens(...):" << std::endl;
  std::cerr << " > max_candidates = " << max_candidates << std::endl;
#endif

  check_context_id(id);
  if (this->id == 0) {
    throw std::runtime_error("Using model #0 (RNG) is not implemented yet!");
  }

  size_t vocab_size = this->vocab_size();
  if (vocab_mask.size() != vocab_size) {
     throw std::runtime_error("vocab_mask size (" + std::to_string(vocab_mask.size()) + ") does not match vocabulary size (" + std::to_string(vocab_size) + ")");
  }

  llama_context * ctx = this->get_context(id);
  TokenSequence const & current_tokens = this->get_tokens_const(id);

  topk_tokens.clear();
  topk_lobprobs.clear();

  std::vector<std::pair<TokenID, float>> candidates;
  sample_logprobs(ctx, vocab_mask, candidates);

  // Handle edge case: no valid candidates
  if (candidates.empty()) {
    throw std::runtime_error("Failed to find candidate token. Cannot have an empty vocabularity mask (all false).");
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

}

