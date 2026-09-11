#ifndef AUTOCOG_BACKEND_LLAMA_SAMPLING_HXX
#define AUTOCOG_BACKEND_LLAMA_SAMPLING_HXX

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <utility>
#include <vector>

// CPU-side logits-row primitives. These run once per evaluated token and were
// the dominant cost of GPU-resident search (~10ms/row of a 128k vocab: a full
// candidate materialization + std::sort to keep k<=8, plus an exact
// log-sum-exp) — see the A100 campaign. Two facts make them cheap:
//
//   * -log P(tok) = LSE - logit[tok] and LSE is a per-row CONSTANT, so
//     ranking by logprob IS ranking by raw logit: top-k needs a k-sized
//     heap over the masked row, not a sort of every allowed candidate.
//   * exp(logit - max) underflows to irrelevance quickly: entries more than
//     LSE_CUTOFF below the max contribute less than vocab*e^-CUTOFF ~ 3e-4
//     nats in total and can skip the exp entirely.

namespace autocog::backend::llama::sampling {

// Entries below max-LSE_CUTOFF are skipped by log_sum_exp; the resulting
// absolute error in nats is bounded by vocab_size * exp(-LSE_CUTOFF)
// (~3e-4 for a 128k vocab at the default 20) — far below anything the
// search reacts to. Adjustable at build time: -DAUTOCOG_LSE_CUTOFF=30.0f
// (or the CMake cache variable AUTOCOG_LSE_CUTOFF); 30 pushes the bound
// under float epsilon, larger values effectively disable the cutoff.
#ifndef AUTOCOG_LSE_CUTOFF
#define AUTOCOG_LSE_CUTOFF 20.0
#endif
float constexpr LSE_CUTOFF = static_cast<float>(AUTOCOG_LSE_CUTOFF);

inline float log_sum_exp(float const * logit, std::size_t vocab_size) {
  float const max_logit = *std::max_element(logit, logit + vocab_size);
  float const floor = max_logit - LSE_CUTOFF;
  float sum = 0.0f;
  for (std::size_t i = 0; i < vocab_size; ++i) {
    if (logit[i] > floor) sum += std::exp(logit[i] - max_logit);
  }
  return max_logit + std::log(sum);
}

// -log P(token) from one logits row.
inline float row_logprob(float const * logits, std::size_t vocab_size, unsigned token) {
  return log_sum_exp(logits, vocab_size) - logits[token];
}

// Masked top-k by raw logit, descending — identical selection and order to
// sorting all candidates by ascending -log P (monotone transform), without
// materializing them. `top` holds (token, raw logit) pairs; convert with a
// single log_sum_exp when actual logprobs are needed. Ties at the heap
// boundary keep the lowest token id (first seen).
inline void topk_masked(float const * row, std::vector<bool> const & mask,
                        std::size_t k,
                        std::vector<std::pair<unsigned, float>> & top) {
  top.clear();
  if (k == 0) return;
  // min-heap on logit: top.front() is the weakest kept candidate
  auto const weaker = [](std::pair<unsigned, float> const & a,
                         std::pair<unsigned, float> const & b) {
    return a.second > b.second;
  };
  std::size_t const n = mask.size();
  for (std::size_t tok = 0; tok < n; ++tok) {
    if (!mask[tok]) continue;
    float const l = row[tok];
    if (top.size() < k) {
      top.emplace_back(static_cast<unsigned>(tok), l);
      std::push_heap(top.begin(), top.end(), weaker);
    } else if (l > top.front().second) {
      std::pop_heap(top.begin(), top.end(), weaker);
      top.back() = {static_cast<unsigned>(tok), l};
      std::push_heap(top.begin(), top.end(), weaker);
    }
  }
  std::sort_heap(top.begin(), top.end(), weaker);  // descending logit
}

} // namespace autocog::backend::llama::sampling

#endif
