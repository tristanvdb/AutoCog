# Micro-benchmarks

Component-level benchmarks for hot primitives, isolated from models, search,
and I/O. Where the compute benchmark (`benchmarks/compute`) measures whole
program evaluations, these measure single functions against the
implementations they replaced — both to quantify the win and to prove
equivalence.

Built as part of the normal CMake tree:

    cmake --build <build-dir> --target autocog_sampling_bench
    <build-dir>/benchmarks/micro/sampling_bench [vocab_size] [rows]

Use a **Release** build: Debug numbers are meaningless (7-8x slower and
differently shaped).

## sampling_bench

Benchmarks the logits-row primitives of `libs/autocog/backend/llama/
sampling.hxx` — the per-evaluated-token cost that dominated GPU-resident
search in the A100 campaign (~10 ms/row of a 128k vocab, 91-99% of eval
time):

| primitive | replaced | replacement |
|---|---|---|
| top-k selection | materialize all masked candidates as (token, logprob) pairs, `std::sort`, keep k | k-sized heap over raw logits (logprob is a monotone transform of logit: identical selection and order) |
| log-sum-exp | exact `exp` over every vocab entry | skip entries below `max - LSE_CUTOFF` (error bound `vocab * e^-CUTOFF` ≈ 3e-4 nats) |

Rows are synthetic: a normal bulk with an implanted winner, in two profiles —
`peaked` (winner far above the bulk, the typical mid-text row) and `flat`
(winner barely above, the high-entropy worst case for the cutoff). Masks
cover the three production regimes: tiny (~10 tokens, digit/select nodes),
default-syntax-like (~74% of vocab), and full vocab.

The run verifies, for every row and mask, that the heap selects exactly the
tokens the sort selected and that logprobs agree within the cutoff bound;
it exits non-zero on any mismatch, so it doubles as a regression test.

Reference numbers (Release, Intel i7-10875H 16t, vocab 128256, k=8):

| mask | sort+LSE ms/row | heap+cutoff ms/row | speedup |
|---|---|---|---|
| tiny (10) | 0.75 | 0.84-0.96 | ~1x |
| default (74%) | 6.9 | 1.2 | ~6x |
| full | 8.8 | 1.0 | ~9x |

In-program measurements are better still (~10 ms → 0.5-1.1 ms per evaluated
token): real rows are more cutoff- and branch-predictor-friendly than the
synthetic bulk. The cutoff itself is roughly neutral on synthetic rows (the
branch costs about what the skipped `exp`s save); the heap is the win.
