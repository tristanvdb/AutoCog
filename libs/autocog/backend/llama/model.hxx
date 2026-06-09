#ifndef AUTOCOG_BACKEND_LLAMA_MODEL_HXX
#define AUTOCOG_BACKEND_LLAMA_MODEL_HXX

#include "autocog/backend/llama/types.hxx"

#include <map>
#include <random>
#include <string>
#include <vector>

namespace autocog::data { struct VocabExpr; }

namespace autocog::backend::llama {

class Model {
  public:
    ModelID const id;

    // RNG model (id=0): byte-level character model with random logprobs
    static constexpr size_t RNG_VOCAB_SIZE = 258;  // 256 byte values + BOS + EOS
    static constexpr TokenID RNG_BOS = 256;
    static constexpr TokenID RNG_EOS = 257;

  private:
    llama_model * model;
    std::vector<llama_context *> contexts;
    std::vector<TokenSequence> tokens;
    std::mt19937 rng;
    std::string source_;                 // GGUF path ("" for the RNG model)
    mutable std::string sha_cache_;      // lazily-computed full SHA-256 of the GGUF

    // Resolved vocab masks for this model, keyed by vocab ref ("vocab_<hash>"),
    // so identical vocabs dedup across every FTA evaluated on this model.
    std::map<std::string, std::vector<bool>> vocab_mask_cache_;
    std::vector<bool> full_vocab_mask_;   // lazily-built all-true mask

    std::vector<bool> build_vocab_mask(autocog::data::VocabExpr const & expr);

    llama_context * get_context(ContextID const id = 0) const;
    TokenSequence & get_tokens(ContextID const id = 0);
    void check_context_id(ContextID const id = 0) const;

    const llama_vocab * get_vocab() const;

  public:
    Model();
    Model(ModelID const id, std::string const & model_path, int n_ctx);
    ~Model();

    void set_seed(unsigned seed) { rng.seed(seed); }

    TokenSequence tokenize(std::string const & text, bool add_bos, bool special);
    std::string detokenize(TokenSequence const & tokens, bool spec_rm, bool spec_unp);

    size_t vocab_size() const;

    // Token mask for a resolved vocab expression, built once and cached per ref.
    // full_vocab_mask() is the unrestricted (all-true) mask.
    std::vector<bool> const & vocab_mask(std::string const & ref, autocog::data::VocabExpr const & expr);
    std::vector<bool> const & full_vocab_mask();

    // Full 64-hex SHA-256 of the backing GGUF file, computed once and cached.
    // The RNG model (no file) reports the sentinel "rng". Used as the model's
    // provenance identity when stamping an evaluated FTT.
    std::string sha256() const;

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

#endif /* AUTOCOG_BACKEND_LLAMA_MODEL_HXX */

