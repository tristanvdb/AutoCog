#ifndef AUTOCOG_RUNTIME_STA_PATH_HXX
#define AUTOCOG_RUNTIME_STA_PATH_HXX

#include <optional>
#include <string>
#include <variant>

namespace autocog::runtime::sta {

// A path-step selector that mirrors STL's grammar faithfully (Python slice
// semantics). A path is a sequence of these, e.g. a.b[4].c[:4].d[2:5].
//
// The selector distinguishes index from slice as a TYPE, so illegal states are
// unrepresentable (an index always has a value and no upper bound; "no bracket"
// is distinct from a full slice, because a bare field may be a scalar):
//
//   g        -> selector = nullopt                 (whole field, scalar or list)
//   b[4]     -> selector = int{4}                  (index -> scalar element)
//   d[2:5]   -> selector = StepRange{2, 5}         (slice -> list)
//   c[:4]    -> selector = StepRange{nullopt, 4}   (open start)
//   e[2:]    -> selector = StepRange{2, nullopt}   (open end)
//   f[:]     -> selector = StepRange{nullopt, nullopt}
//
// Bounds are resolved ints (all STL expressions are constexpr; resolved at
// instantiation). nullopt bounds mean open (Python: lower=0, upper=len).
struct StepRange {
    std::optional<int> lower;   // nullopt = open start
    std::optional<int> upper;   // nullopt = open end
};

struct PathStep {
    std::string name;
    std::optional<std::variant<int, StepRange>> selector;  // nullopt = whole field
};

}

#endif // AUTOCOG_RUNTIME_STA_PATH_HXX
