#ifndef AUTOCOG_UTILITIES_TYPES_HXX
#define AUTOCOG_UTILITIES_TYPES_HXX

#include <map>
#include <string>
#include <variant>
#include <vector>

namespace autocog::types {

/// A resolved scalar value. Every STL expression is constexpr, so by the time a
/// value is carried through the IR / data artifacts it is concrete. This is the
/// single source of truth for that scalar shape, shared across the data types,
/// the runtime, and the compiler IR. `monostate` denotes JSON null / Python None.
using Value = std::variant<int, float, bool, std::string, std::monostate>;

/// Dynamic, nested runtime data: the in-memory shape of channel inputs
/// (`content`) and walked outputs (`frame`). This is the JSON-free currency the
/// runtime libs trade in; JSON / Python conversion lives in autocog::codec and
/// is applied only at the tool / binding boundary.
///
/// A Document is a scalar leaf, an ordered list, or a name-keyed object. Key
/// order is not semantically meaningful: a Document is transient (never hashed)
/// and the canonical field order lives in the STA's field table, so the map need
/// not carry it. `std::map` (not `unordered_map`) is deliberate: it is the only
/// associative container libstdc++ instantiates with an incomplete value type,
/// which the recursive `Object`/`Array` definitions require (GCC 11 evaluates
/// `std::variant`'s copy-ctor trait eagerly; `unordered_map` then forces the
/// incomplete `Document` to complete and fails to compile).
struct Document {
  using Array  = std::vector<Document>;
  using Object = std::map<std::string, Document>;

  std::variant<Value, Array, Object> value;
};

}

#endif // AUTOCOG_UTILITIES_TYPES_HXX
