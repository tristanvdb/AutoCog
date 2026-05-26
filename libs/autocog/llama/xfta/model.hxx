#ifndef AUTOCOG_LLAMA_XFTA_MODEL_HXX
#define AUTOCOG_LLAMA_XFTA_MODEL_HXX

#include "autocog/llama/xfta/types.hxx"

#include <string>

namespace autocog::llama::xfta {

class Model {
  public:
    ModelID const id;

  private:
    llama_model * model;
    std::vector<llama_context *> contexts;
    std::vector<TokenSequence> tokens;

    llama_context * get_context(ContextID const id = 0) const;
    TokenSequence & get_tokens(ContextID const id = 0);
    void check_context_id(ContextID const id = 0) const;

    const llama_vocab * get_vocab() const;

  public:
    Model();
    Model(ModelID const id, std::string const & model_path, int n_ctx);
    ~Model();

    ContextID fork_context(ContextID const id = 0);

    TokenSequence tokenize(std::string const & text, bool add_bos, bool special);
    std::string detokenize(TokenSequence const & tokens, bool spec_rm, bool spec_unp);
    
    size_t vocab_size() const;
    TokenID bos_token() const;
    TokenID eos_token() const;

    TokenSequence const & get_tokens_const(ContextID const id = 0) const;

    unsigned set_tokens(
      TokenSequence const & tokens,
      ContextID const id = 0
    );

    unsigned eval_sequences(
      TokenSequence const & tokens,
      ProbaSequence & logprobs,
      ContextID const id = 0
    );

    unsigned eval_topk_tokens(
      std::vector<bool> const & vocab_mask,
      size_t max_candidates,
      std::vector<TokenID> & topk_tokens,
      std::vector<float> & topk_logprobs,
      ContextID const id
    );
};

}

#endif /* AUTOCOG_LLAMA_XFTA_MODEL_HXX */

