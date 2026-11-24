#ifndef AUTOCOG_UTILITIES_PROFILING_HXX
#define AUTOCOG_UTILITIES_PROFILING_HXX

/// Fine-grain profiling: per-event records and function-scope timers.
///
/// Compiled out entirely unless the build defines AUTOCOG_PROFILE=1
/// (`cmake -DAUTOCOG_PROFILE=ON`). When off, every macro below expands to
/// `((void)0)`: the arguments are never evaluated, no registry exists, and
/// nothing is linked in. A production build carries none of this.
///
/// The records are emitted as ECS-flavored NDJSON through the same stream as
/// the always-on `autocog.perf.*` aggregates (see
/// backend/llama/perf-fields.hxx), distinguished by `event.action` so a
/// consumer can filter one from the other.
///
/// Two kinds of record:
///
/// - **events** — one object per occurrence, for things whose *distribution*
///   is the question (`kv.access`: which slot, how deep the prefix reuse was,
///   whether it cost a decode). Aggregates cannot answer those.
/// - **scopes** — call count and wall time per named region, for the
///   llama.cpp API boundary and AutoCog's own hot functions. The container
///   this runs in has no usable sampling profiler, so wrapping the boundary is
///   the only way to attribute time inside llama.cpp.

#include <nlohmann/json.hpp>

#include <chrono>
#include <cstdint>
#include <map>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

namespace autocog::profiling {

/// Accumulated wall time for one named scope.
struct ScopeStat {
    uint64_t calls = 0;
    uint64_t nanoseconds = 0;
};

#if AUTOCOG_PROFILE

/// Process-wide sink for fine-grain records.
///
/// Thread-safe: the Manager can drive several models at once, and a scope
/// timer may close on any of their threads. The lock is uncontended in the
/// common single-evaluation case and this only exists in profiling builds, so
/// the cost is not on any production path.
class Registry {
  public:
    static Registry & instance() {
        static Registry registry;
        return registry;
    }

    /// Record one occurrence. `action` becomes the record's `event.action`.
    void event(char const * action, nlohmann::json fields) {
        std::lock_guard<std::mutex> lock(mutex_);
        events_.emplace_back(action, std::move(fields));
    }

    void scope(char const * name, uint64_t nanoseconds) {
        std::lock_guard<std::mutex> lock(mutex_);
        ScopeStat & st = scopes_[name];
        st.calls += 1;
        st.nanoseconds += nanoseconds;
    }

    /// Take everything collected so far, leaving the registry empty. The
    /// caller owns the result and decides how to serialize it.
    std::pair<std::vector<std::pair<std::string, nlohmann::json>>,
              std::map<std::string, ScopeStat>> drain() {
        std::lock_guard<std::mutex> lock(mutex_);
        auto out = std::make_pair(std::move(events_), std::move(scopes_));
        events_.clear();
        scopes_.clear();
        return out;
    }

  private:
    std::mutex mutex_;
    std::vector<std::pair<std::string, nlohmann::json>> events_;
    std::map<std::string, ScopeStat> scopes_;
};

/// Charges the wall time of its enclosing block to `name`.
class ScopeTimer {
  public:
    explicit ScopeTimer(char const * name)
      : name_(name), start_(std::chrono::steady_clock::now()) {}

    ~ScopeTimer() {
        auto const elapsed = std::chrono::steady_clock::now() - start_;
        Registry::instance().scope(
            name_, static_cast<uint64_t>(
                std::chrono::duration_cast<std::chrono::nanoseconds>(elapsed).count()));
    }

    ScopeTimer(ScopeTimer const &) = delete;
    ScopeTimer & operator=(ScopeTimer const &) = delete;

  private:
    char const * name_;
    std::chrono::steady_clock::time_point start_;
};

#define AUTOCOG_PROF_ENABLED 1

#define AUTOCOG_PROF_CAT_IMPL(a, b) a##b
#define AUTOCOG_PROF_CAT(a, b) AUTOCOG_PROF_CAT_IMPL(a, b)

/// Time the enclosing block, charging it to `name`.
#define AUTOCOG_PROF_SCOPE(name) \
    ::autocog::profiling::ScopeTimer AUTOCOG_PROF_CAT(autocog_prof_scope_, __LINE__)(name)

/// Record one occurrence; `fields` is an nlohmann::json object.
#define AUTOCOG_PROF_EVENT(action, ...) \
    ::autocog::profiling::Registry::instance().event(action, __VA_ARGS__)

#else

#define AUTOCOG_PROF_ENABLED 0
#define AUTOCOG_PROF_SCOPE(name) ((void)0)
#define AUTOCOG_PROF_EVENT(action, ...) ((void)0)

#endif

} // namespace autocog::profiling

#endif
