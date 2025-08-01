#include "autocog/llama/model.hxx"

#include <llama.h>

#include <stdexcept>

namespace autocog { namespace llama {

Model::Model(std::string const & model_path, int n_ctx) : model(nullptr), contexts() {
  llama_backend_init();
   
  // Load model
  llama_model_params model_params = llama_model_default_params();
  this->model = llama_model_load_from_file(model_path.c_str(), model_params);
  if (!this->model) {
    throw std::runtime_error("Failed to load model from: " + model_path);
  }
   
  // Create context parameters
  llama_context_params ctx_params = llama_context_default_params();
  ctx_params.n_ctx = n_ctx;
   
  // Create single context with ID=0
  llama_context * ctx = llama_init_from_model(this->model, ctx_params);
  if (!ctx) {
    llama_model_free(this->model);
    throw std::runtime_error("Failed to create llama context");
  }
  this->contexts.push_back(ctx);
}

Model::~Model() {
  for (auto* ctx : this->contexts) if (ctx) llama_free(ctx);
  contexts.clear();

  if (this->model) llama_free_model(model);

  llama_backend_free();
}

llama_context * Model::get_context(ContextID const id) const {
  if (id >= this->contexts.size()) {
    throw std::runtime_error("Invalid context ID: " + std::to_string(id));
  }
  return this->contexts[id];
}

ContextID Model::fork_context(ContextID const id) {
  throw std::runtime_error("Context forking is not implemented yet (Phase 3 feature)");
}

TokenSequence Model::tokenize(std::string const & text, bool add_bos, bool special) {   
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
  return llama_model_get_vocab(this->model);
}

size_t Model::vocab_size() const {
   return llama_vocab_n_tokens(this->get_vocab());
}

TokenID Model::bos_token() const {
   return llama_vocab_bos(this->get_vocab());
}

TokenID Model::eos_token() const {
   return llama_vocab_eos(this->get_vocab());
}

std::vector<float> Model::get_logits(ContextID const id) {
  float * logits = llama_get_logits(this->get_context(id));
  if (!logits) {
    throw std::runtime_error("Failed to get logits - context may not have been evaluated");
  }
  return std::vector<float>(logits, logits + this->vocab_size());
}

bool Model::eval_tokens(TokenSequence const & tokens, ContextID const id) {
  if (tokens.empty()) {
    return true;
  }

  llama_batch batch = llama_batch_get_one(const_cast<TokenID*>(tokens.data()), tokens.size());
  int result = llama_decode(this->get_context(id), batch);

  return result == 0;
}

} }
