#ifndef AUTOCOG_BACKEND_LLAMA_PREPARED_HXX
#define AUTOCOG_BACKEND_LLAMA_PREPARED_HXX

#include "autocog/backend/llama/types.hxx"

#include "autocog/data/fta.hxx"
#include "autocog/data/ftt.hxx"

#include <vector>

namespace autocog::backend::llama {

/// Model-bound tokenization of one FTA action, parallel to data::FTA::actions.
/// Only the field matching the action's body kind is filled; the rest stay
/// empty. Vocab masks are NOT here -- they live in the per-model cache (Model),
/// keyed by vocab ref so they dedup across every FTA on that model.
struct PreparedAction {
  std::vector<unsigned> successors;     ///< FTA successor uids resolved to dense indices.
  TokenSequence tokens;                 ///< Text: tokenized text.
  std::vector<TokenSequence> choices;   ///< Choose: tokenized choices.
};

/// A portable data::FTA plus its model-bound tokenization. The FTA is held by
/// reference (the caller owns it for the evaluation's lifetime); the caches are
/// indexed by the dense action id (position in fta.actions).
struct PreparedFTA {
  data::FTA const & fta;
  std::vector<PreparedAction> actions;  ///< Parallel to fta.actions.
  double prepare_seconds = 0.0;         ///< prepare() wall time (tokenization + mask priming).
};

/// Tokenize an FTA for a model: resolve successors and tokenize text/choices,
/// and prime the model's mask cache for each action's generation and stop
/// vocabs. Masks are resolved by the engine via the model cache.
PreparedFTA prepare(ModelID const model, data::FTA const & fta);

/// Fill every node's detokenized `text` from its `tokens`, in place. Run once
/// after generation completes.
void detokenize(ModelID const model, data::FTT & ftt);

/// Inverse of detokenize: fill every node's tokens (zero logprobs, cumulative
/// lengths) from its text, materializing a text-level FTT — e.g. one produced
/// by the frame encoder — under this model's tokenizer.
void tokenize(ModelID const model, data::FTT & ftt);

/// Score every node's tokens against the model as a forced path:
/// P(token | everything above it in the tree), filling logprobs/logprob in
/// place. On an encoded FTT this measures constraint friction — how hard the
/// model fights the structural tokens it never chose. The root (the
/// unconditioned prompt) keeps zero logprobs.
void score(ModelID const model, data::FTT & ftt);

}

#endif // AUTOCOG_BACKEND_LLAMA_PREPARED_HXX
