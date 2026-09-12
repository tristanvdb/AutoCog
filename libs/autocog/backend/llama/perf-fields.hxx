#ifndef AUTOCOG_BACKEND_LLAMA_PERF_FIELDS_HXX
#define AUTOCOG_BACKEND_LLAMA_PERF_FIELDS_HXX

#include "autocog/backend/llama/model.hxx"
#include "autocog/backend/llama/evaluation.hxx"

#include <nlohmann/json.hpp>

#include <string>

// The single source of the `autocog.perf.*` field map: xfta --perf and the
// Python bindings both assemble the same flat ECS-flavored dictionary from
// here, so the two surfaces cannot drift. Model-level stats (decode/kv)
// accumulate across evaluations on a shared model; callers wanting per-eval
// numbers pass the pre-evaluation snapshot as the baseline (xfta runs one
// eval per process, so its baseline is default-constructed zeros).

namespace autocog::backend::llama {

inline DecodeStats operator-(DecodeStats a, DecodeStats const & b) {
    a.calls          -= b.calls;
    a.decode_seconds -= b.decode_seconds;
    a.sample_seconds -= b.sample_seconds;
    a.score_seconds  -= b.score_seconds;
    return a;
}

inline KvStats operator-(KvStats a, KvStats const & b) {
    a.exact         -= b.exact;
    a.extends       -= b.extends;
    a.trims         -= b.trims;
    a.forks         -= b.forks;
    a.evictions     -= b.evictions;
    a.tokens_primed -= b.tokens_primed;
    return a;
}

inline nlohmann::json kind_fields(std::string const & kind,
                                  PerfCounters::KindStats const & st) {
    std::string p = "autocog.perf." + kind + ".";
    return {
        {p + "calls", st.calls},
        {p + "seconds", st.seconds},
        {p + "tokens.restore", st.tokens_restore},
        {p + "tokens.eval", st.tokens_eval},
    };
}

inline nlohmann::json perf_fields(Model const & model, Evaluation const & eval,
                                  DecodeStats const & ds_base = {},
                                  KvStats const & kv_base = {}) {
    PerfCounters const & pc = eval.perf();
    // Bind each kind_fields() result to a named json before iterating:
    // items() returns a proxy referencing its json, and a temporary in a
    // range-for initializer is destroyed before the loop body runs (pre-C++23).
    nlohmann::json f = kind_fields("text", pc.text);
    nlohmann::json const fcomplete = kind_fields("complete", pc.complete);
    for (auto const & [k, v] : fcomplete.items()) f[k] = v;
    nlohmann::json const fchoose = kind_fields("choose", pc.choose);
    for (auto const & [k, v] : fchoose.items()) f[k] = v;
    f["autocog.perf.complete.tokens.lookahead"] = pc.lookahead_tokens;
    f["autocog.perf.prepare_seconds"] = pc.prepare_seconds;
    f["autocog.perf.advance_seconds"] = pc.advance_seconds;
    f["autocog.perf.tokens.restore"] =
        pc.text.tokens_restore + pc.complete.tokens_restore + pc.choose.tokens_restore;
    f["autocog.perf.tokens.eval"] =
        pc.text.tokens_eval + pc.complete.tokens_eval + pc.choose.tokens_eval;
    KvStats const kv = model.kv_stats() - kv_base;
    f["autocog.perf.kv.slots"] = model.kv_slots();
    f["autocog.perf.kv.exact"] = kv.exact;
    f["autocog.perf.kv.extends"] = kv.extends;
    f["autocog.perf.kv.trims"] = kv.trims;
    f["autocog.perf.kv.forks"] = kv.forks;
    f["autocog.perf.kv.evictions"] = kv.evictions;
    f["autocog.perf.kv.tokens.primed"] = kv.tokens_primed;
    DecodeStats const ds = model.decode_stats() - ds_base;
    f["autocog.perf.decode.calls"] = ds.calls;
    f["autocog.perf.decode.seconds"] = ds.decode_seconds;
    f["autocog.perf.sample.seconds"] = ds.sample_seconds;
    f["autocog.perf.score.seconds"] = ds.score_seconds;
    SearchStats const st = eval.search_stats();
    f["autocog.perf.search.terminals"] = st.terminals;
    f["autocog.perf.search.coverage.fta"] = st.coverage_fta;
    f["autocog.perf.search.coverage.sta"] = st.coverage_sta;
    f["autocog.perf.search.stopped"] = st.stopped;
    f["autocog.perf.search.abandoned"] = st.abandoned;
    return f;
}

} // namespace autocog::backend::llama

#endif
