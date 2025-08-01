#ifndef __AUTOCOG_LLAMA_MODEL__HXX_
#define __AUTOCOG_LLAMA_MODEL__HXX_

#include "autocog/llama/types.hxx"

#include <string>

namespace autocog {
namespace llama {

class Model {
  private:
    llama_model * model;
    std::vector<llama_context *> contexts;

    llama_context * get_context(ContextID const id = 0) const;
    const llama_vocab * get_vocab() const;

  public:
    Model(std::string const & model_path, int n_ctx);
    ~Model();

    ContextID fork_context(ContextID const id = 0);

    TokenSequence tokenize(std::string const & text, bool add_bos, bool special);
    std::string detokenize(TokenSequence const & tokens, bool spec_rm, bool spec_unp);
    
    size_t vocab_size() const;
    TokenID bos_token() const;
    TokenID eos_token() const;

    std::vector<float> get_logits(ContextID const id = 0);
    bool eval_tokens(const TokenSequence& tokens, ContextID const id = 0);
};

} }

#endif /* __AUTOCOG_LLAMA_MODEL__HXX_ */

