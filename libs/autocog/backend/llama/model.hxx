#ifndef AUTOCOG_BACKEND_LLAMA_MODEL_HXX
#define AUTOCOG_BACKEND_LLAMA_MODEL_HXX

#include "autocog/backend/llama/types.hxx"

#include <map>
#include <random>
#include <string>
#include <vector>

namespace autocog::data { struct VocabExpr; }

namespace autocog::backend::llama {

// Counters for the KV sequence-slot pool, reported by xfta --perf. Each
// set_tokens call resolves to exactly one of exact/extend/trim/fork.
struct KvStats {
  unsigned exact = 0;          ///< target fully cached in a slot, zero decode
  unsigned extends = 0;        ///< slot was a strict prefix, appended in place
  unsigned trims = 0;          ///< slot was a strict extension, tail removed
  unsigned forks = 0;          ///< shared prefix copied (llama_memory_seq_cp), suffix decoded
  unsigned evictions = 0;      ///< slots dropped (LRU victim reuse or KV-full recovery)
  unsigned tokens_primed = 0;  ///< single-token re-decodes refreshing final-position logits
};

// Masked top-k candidates at one frontier position (see Model::topk_frontier).
struct FrontierResult {
  std::vector<TokenID> tokens;
  std::vector<float> logprobs;
};

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
    std::vector<TokenSequence> tokens;   // per-context record (RNG model only)
    std::mt19937 rng;

    // KV sequence-slot pool (real models, context 0). Each slot is one llama
    // sequence id in the shared (kv_unified) KV cache, remembering the token
    // sequence it holds. set_tokens routes each target to the slot with the
    // longest common prefix: exact/extend/trim reuse it in place, a divergent
    // target *forks* — llama_memory_seq_cp of the shared prefix (metadata-only
    // in the unified cache) into an LRU victim, then decode just the suffix.
    // This keeps sibling branches resident during beam/choice ping-pong
    // instead of re-decoding their suffixes on every switch.
    struct Slot {
      TokenSequence tokens;
      uint64_t last_used = 0;
    };
    std::vector<Slot> slots_;
    size_t active_slot_ = 0;
    int live_logits_slot_ = -1;  ///< slot whose final position produced the live logits
    int live_logits_ith_ = -1;   ///< batch index of that final position's logits row
    uint64_t slot_clock_ = 0;
    KvStats kv_stats_;

    // Decode `n` tokens into `slot` starting at position `pos0`, requesting
    // logits at the final position (or, with `all_logits`, at every position —
    // rows then read back via llama_get_logits_ith by batch index). On KV-cell
    // exhaustion, evicts every other slot and retries once. Returns n.
    unsigned decode_extension(size_t slot, int pos0, TokenID const * toks, size_t n,
                              ContextID const id, bool all_logits = false);
    size_t pick_victim(size_t keep) const;

    // The live logits row: the active slot's final-position distribution.
    float const * live_row(ContextID const id) const;
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

    // Move-only: a Model owns raw llama_model*/llama_context* handles, so a copy
    // would alias them and double-free at teardown. The noexcept move ctor lets
    // the Manager's std::vector<Model> relocate on growth by transferring
    // ownership (and nulling the source) instead of copying; the copy operations
    // are deleted so an accidental shallow copy can never compile. (Move
    // assignment is implicitly deleted by the const `id` member and is not needed
    // for vector growth, which relocates via move construction.)
    Model(Model const &) = delete;
    Model & operator=(Model const &) = delete;
    Model(Model && other) noexcept;

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

    KvStats const & kv_stats() const { return kv_stats_; }
    size_t kv_slots() const { return slots_.size(); }

    // Route `tokens` to a KV slot (see the slot-pool comment above), returning
    // the number of tokens decoded doing so. With `prime_logits`, guarantees
    // the live logits are those of the target's final position on return —
    // required before eval_topk_tokens; costs one re-decoded token when the
    // target was fully cached.
    unsigned set_tokens(
      TokenSequence const & tokens,
      ContextID const id = 0,
      bool prime_logits = false
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

    // Masked top-k over a whole frontier of alternative continuations in as
    // few llama_decode calls as slot capacity allows. Each target is routed
    // to a slot like set_tokens (slots already carrying batch-pending tokens
    // are pinned: never trimmed or victimized, and fork points are clamped to
    // materialized cells); all extensions plus a final-token re-decode for
    // fully-cached targets form one batch, with a logits row per target's
    // final position. On CPU a decode call streams the full weights whatever
    // its size, so one call for N frontier tokens costs roughly one token's
    // wall time. Returns the number of tokens decoded (>= targets.size();
    // the excess is fork/suffix restoration work). The RNG model runs the
    // targets sequentially, preserving the historical draw order.
    unsigned topk_frontier(
      std::vector<TokenSequence> const & targets,
      std::vector<bool> const & vocab_mask,
      size_t max_candidates,
      std::vector<FrontierResult> & results,
      ContextID const id = 0
    );
};

}

#endif /* AUTOCOG_BACKEND_LLAMA_MODEL_HXX */

