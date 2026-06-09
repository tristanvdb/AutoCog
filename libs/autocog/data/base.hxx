#ifndef AUTOCOG_DATA_BASE_HXX
#define AUTOCOG_DATA_BASE_HXX

#include "autocog/data/metadata.hxx"
#include "autocog/data/utility.hxx"
#include "autocog/utilities/errors.hxx"

#include <map>
#include <optional>
#include <string>

namespace autocog::data {

/// CRTP base shared by every stored artifact (STA, FTA, FTT, Syntax,
/// SearchConfig).
///
/// It owns the identity/provenance fields and the finalize/hash surface; each
/// derived @p CRT supplies its own content hash through `content_hash()`.
/// Conversion lives in the free functions in json.hxx / python.hxx, not here,
/// so this header carries no nlohmann/pybind dependency. Nothing here is
/// virtual: dispatch to the derived is static, via `static_cast<CRT const *>`.
template <class CRT>
class Base {
  public:
    /// Identity header. Absent before finalize() (construct mode); present
    /// after, which is what enables verification on load.
    std::optional<Metadata> metadata;

    /// Transitive closure of ancestor identities, keyed by role
    /// (e.g. "syntax", "search", "fta"). Part of the hash.
    std::map<std::string, std::string> provenance;

  public:
    /// Construct mode (metadata absent) stamps a fresh header; verify mode
    /// (metadata present) recomputes the hash and throws on mismatch.
    void finalize();

  protected:
    /// The artifact identity: the hash of the provenance combined with the
    /// derived content hash. Metadata is not an input (it stores this value).
    std::string hash() const;
};

}

#include "autocog/data/base.txx"

#endif // AUTOCOG_DATA_BASE_HXX
