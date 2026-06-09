#ifndef AUTOCOG_DATA_PATH_HXX
#define AUTOCOG_DATA_PATH_HXX

#include <optional>
#include <string>
#include <variant>

namespace autocog::data {

/// A slice bound pair (Python semantics). nullopt bounds are open.
struct StepRange {
  std::optional<int> lower;
  std::optional<int> upper;
};

/// One step of a path: a field name plus an optional selector. The selector
/// distinguishes "whole field" (nullopt), a single "index", and a "slice", so
/// illegal states are unrepresentable. Conversion lives in the free functions
/// in json.hxx / python.hxx.
struct PathStep {
  std::string name;
  std::optional<std::variant<int, StepRange>> selector;

  std::string hash() const;
};

}

#endif // AUTOCOG_DATA_PATH_HXX
