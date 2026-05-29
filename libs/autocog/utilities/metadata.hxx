#ifndef AUTOCOG_UTILITIES_METADATA_HXX
#define AUTOCOG_UTILITIES_METADATA_HXX

#include <nlohmann/json.hpp>
#include <string>

namespace autocog {

/// Compute the UID of a JSON artifact.
/// Removes metadata.uid before hashing (circular dependency).
/// Returns a 16-char hex string (truncated SHA-256 of compact JSON).
std::string compute_uid(nlohmann::json artifact);

/// Compute the source UID from raw file content.
/// Returns a 16-char hex string (truncated SHA-256).
std::string compute_source_uid(std::string const & content);

/// Build a metadata block for a generated artifact.
/// @param format   "ir", "sta", or "fta"
/// @param source   UID of the source artifact (or source file hash)
/// @param artifact The artifact JSON (used to compute uid)
/// @return A metadata JSON object with format, version, uid, source_uid, timestamp
nlohmann::json build_metadata(std::string const & format,
                              std::string const & source_uid);

/// Attach metadata to an artifact JSON object.
/// Computes uid from the artifact content (with metadata.uid absent).
void attach_metadata(nlohmann::json & artifact,
                     std::string const & format,
                     std::string const & source_uid);

} // namespace autocog

#endif
