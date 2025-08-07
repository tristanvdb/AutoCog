#ifndef __AUTOCOG_LLAMA_MODEL__HXX_
#define __AUTOCOG_LLAMA_MODEL__HXX_

#include "autocog/llama/types.hxx"

#include <string>

namespace autocog {
namespace llama {

class Model {
  private:
    ModelID const id;
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
      ProbaSequence & probas,
      ContextID const id = 0
    );

    unsigned eval_topk_tokens(
      std::vector<bool> const & vocab_mask,
      size_t max_candidates,
      std::vector<TokenID> & topk_tokens,
      std::vector<float> & topk_probas,
      ContextID const id
    );
};

} }

#endif /* __AUTOCOG_LLAMA_MODEL__HXX_ */

