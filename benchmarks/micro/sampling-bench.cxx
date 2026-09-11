// Micro-benchmark of the CPU-side logits-row primitives (sampling.hxx)
// against the implementations they replaced: full-candidate std::sort and
// exact log-sum-exp. Verifies equivalence (same tokens, logprobs within the
// documented LSE-cutoff bound) and reports per-row costs — the per-evaluated-
// token numbers that dominated GPU-resident search (~10ms/row).
//
//   sampling_bench [vocab_size] [rows]        (defaults: 128256, 50)
//
// Two synthetic row profiles bracket real model rows:
//   peaked: one clear winner far above a normal bulk (typical mid-text row)
//   flat:   winner barely above the bulk (high-entropy row) — the worst
//           case for the LSE cutoff, since fewer entries can be skipped.
// Three masks: tiny (10 tokens, digits-like), default-like (~74% of vocab),
// full. k = 8 candidates throughout.

#include "autocog/backend/llama/sampling.hxx"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <random>
#include <string>
#include <vector>

namespace sampling = autocog::backend::llama::sampling;

// ---- reference implementations (what model.cxx did before) ----

static float naive_log_sum_exp(float const * logit, size_t n) {
  float max_logit = *std::max_element(logit, logit + n);
  float sum = 0.0f;
  for (size_t i = 0; i < n; ++i) sum += std::exp(logit[i] - max_logit);
  return max_logit + std::log(sum);
}

static void naive_topk(float const * logits, std::vector<bool> const & mask,
                       size_t k, std::vector<std::pair<unsigned, float>> & out) {
  float const lse = naive_log_sum_exp(logits, mask.size());
  std::vector<std::pair<unsigned, float>> candidates;
  for (unsigned tok = 0; tok < mask.size(); tok++)
    if (mask[tok]) candidates.emplace_back(tok, lse - logits[tok]);
  std::sort(candidates.begin(), candidates.end(),
            [](const auto & a, const auto & b) { return a.second < b.second; });
  candidates.resize(std::min(k, candidates.size()));
  out = std::move(candidates);
}

// ---- harness ----

static double ms_per_row(std::vector<std::vector<float>> const & rows, auto && fn) {
  auto const t0 = std::chrono::steady_clock::now();
  for (auto const & row : rows) fn(row.data());
  auto const dt = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0);
  return 1e3 * dt.count() / rows.size();
}

int main(int argc, char ** argv) {
  size_t const vocab = argc > 1 ? std::strtoul(argv[1], nullptr, 10) : 128256;
  size_t const nrows = argc > 2 ? std::strtoul(argv[2], nullptr, 10) : 50;
  size_t const k = 8;

  std::mt19937 rng(42);
  std::normal_distribution<float> bulk(0.0f, 3.0f);

  struct Profile { char const * name; float winner; };
  struct Mask { char const * name; std::vector<bool> mask; };

  std::vector<Mask> masks;
  masks.push_back({"tiny(10)", std::vector<bool>(vocab, false)});
  for (size_t i = 0; i < 10; ++i) masks[0].mask[rng() % vocab] = true;
  masks.push_back({"default(74%)", std::vector<bool>(vocab, false)});
  for (size_t i = 0; i < vocab; ++i) masks[1].mask[i] = (rng() % 100) < 74;
  masks.push_back({"full", std::vector<bool>(vocab, true)});

  double worst_err = 0.0;
  size_t mismatches = 0;

  std::printf("vocab=%zu rows=%zu k=%zu (times in ms/row)\n\n", vocab, nrows, k);
  std::printf("%-8s %-13s | %9s %9s %7s | %9s %9s %7s\n",
              "profile", "mask", "sort+LSE", "heap+cut", "speedup",
              "LSE-exact", "LSE-cut", "speedup");

  for (Profile const & p : {Profile{"peaked", 18.0f}, Profile{"flat", 9.0f}}) {
    std::vector<std::vector<float>> rows(nrows);
    for (auto & row : rows) {
      row.resize(vocab);
      for (auto & x : row) x = bulk(rng);
      row[rng() % vocab] = p.winner;  // the clear (or not so clear) winner
    }

    for (Mask const & m : masks) {
      // equivalence: identical tokens, logprobs within the documented bound
      for (auto const & row : rows) {
        std::vector<std::pair<unsigned, float>> ref, top;
        naive_topk(row.data(), m.mask, k, ref);
        sampling::topk_masked(row.data(), m.mask, k, top);
        float const lse = sampling::log_sum_exp(row.data(), vocab);
        for (size_t i = 0; i < ref.size(); ++i) {
          if (top[i].first != ref[i].first) ++mismatches;
          worst_err = std::max(worst_err,
              (double)std::abs((lse - top[i].second) - ref[i].second));
        }
      }

      std::vector<std::pair<unsigned, float>> scratch;
      double const t_ref = ms_per_row(rows, [&](float const * r) {
        naive_topk(r, m.mask, k, scratch);
      });
      double const t_new = ms_per_row(rows, [&](float const * r) {
        sampling::topk_masked(r, m.mask, k, scratch);
        volatile float lse = sampling::log_sum_exp(r, vocab);
        (void)lse;
      });
      double const t_lse_ref = ms_per_row(rows, [&](float const * r) {
        volatile float lse = naive_log_sum_exp(r, vocab);
        (void)lse;
      });
      double const t_lse_new = ms_per_row(rows, [&](float const * r) {
        volatile float lse = sampling::log_sum_exp(r, vocab);
        (void)lse;
      });
      std::printf("%-8s %-13s | %9.3f %9.3f %6.1fx | %9.3f %9.3f %6.1fx\n",
                  p.name, m.name, t_ref, t_new, t_ref / t_new,
                  t_lse_ref, t_lse_new, t_lse_ref / t_lse_new);
    }
  }

  std::printf("\nequivalence: %zu token mismatches, worst logprob error %.2e"
              " (bound: vocab*e^-%g = %.2e)\n",
              mismatches, worst_err, (double)sampling::LSE_CUTOFF,
              vocab * std::exp(-sampling::LSE_CUTOFF));
  bool const ok = mismatches == 0
      && worst_err < vocab * std::exp(-sampling::LSE_CUTOFF) + 1e-6;
  std::printf("%s\n", ok ? "PASS" : "FAIL");
  return ok ? 0 : 1;
}
