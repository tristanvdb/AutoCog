#ifndef AUTOCOG_DATA_METADATA_HXX
#define AUTOCOG_DATA_METADATA_HXX

#include "autocog/data/utility.hxx"

#include <string>
#include <utility>

namespace autocog::data {

/// Schema version of the serialized artifacts, tied to the AutoCog version
/// (the schema only changes with the AutoCog minor version). Provided by CMake
/// via the AUTOCOG_VERSION compile definition.
inline constexpr char const * schema_version = AUTOCOG_VERSION;

/// Identity header attached to an artifact at finalize().
///
/// None of these fields feed the content hash: @ref hash *is* the computed
/// identity (over the artifact content plus its provenance), so it cannot be
/// one of its own inputs, and @ref timestamp only records when the artifact
/// was finalized. Conversion lives in the free functions in json.hxx /
/// python.hxx.
struct Metadata {
  std::string format;     ///< Artifact kind ("sta", "fta", "ftt", "syntax", "search").
  std::string version;    ///< Schema version of the serialized form.
  std::string hash;       ///< The artifact's identity (content + provenance).
  std::string timestamp;  ///< RFC 3339 UTC time the artifact was finalized.

  /// Build a fresh header for a finalized artifact of type @p CRT: format from
  /// CRT::format, version from schema_version.
  template <class CRT>
  static Metadata make(std::string hash);
};

}

#include "autocog/data/metadata.txx"

#endif // AUTOCOG_DATA_METADATA_HXX
