#include "autocog/llama/model.hxx"

#include <llama.h>

#include <stdexcept>

namespace autocog { namespace llama {

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
  for (auto* ctx : this->contexts) if (ctx) llama_free(ctx);
  contexts.clear();

  if (this->model) llama_model_free(model);
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

static unsigned run_batch_full(llama_context * ctx, TokenSequence const & tokens) {
  llama_memory_t mem = llama_get_memory(ctx);
  llama_memory_clear(mem, false); 

  unsigned n_ctx = llama_n_ctx(ctx);
  if (tokens.size() > n_ctx) {
    throw std::runtime_error("Token sequence too long: " + std::to_string(tokens.size()) + " > " + std::to_string(n_ctx));
  }
  llama_batch batch = llama_batch_get_one(const_cast<TokenID*>(tokens.data()), tokens.size());
  if (llama_decode(ctx, batch) != 0) {
    throw std::runtime_error("Failed to set the token sequence.");
  }
  return tokens.size();
}

unsigned Model::set_tokens(TokenSequence const & tokens_, ContextID const id) {
  if (this->id == 0) {
    return tokens_.size();
  }
  check_context_id(id);

  TokenSequence & loc_tokens = this->get_tokens(id);
  loc_tokens = tokens_;
  return run_batch_full(this->get_context(id), loc_tokens);
}

unsigned Model::eval_sequences(TokenSequence const & new_tokens, ProbaSequence & probas, ContextID const id) {
  if (this->id == 0) {
    throw std::runtime_error("Using model #0 (RNG) is not implemented yet!");
    // TODO fill `probas` with random float in [0,1]
    return new_tokens.size();
  }

  TokenSequence & loc_tokens = this->get_tokens(id);
  llama_pos token_pos = loc_tokens.size();
  probas.clear();

  for (auto token: new_tokens) {

    llama_batch batch = llama_batch_get_one(&token, 1);
    batch.pos = &token_pos;

    if (llama_decode(this->get_context(id), batch) != 0) {
      throw std::runtime_error("Failed to decode token");
    }

    float tok_probas = llama_get_logits(this->get_context(id))[token];
    probas.push_back(tok_probas);

    token_pos++;
  }
  loc_tokens.insert(loc_tokens.end(), new_tokens.begin(), new_tokens.end());
  return new_tokens.size();
}

} }

