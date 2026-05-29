
#include "autocog/utilities/metadata.hxx"
#include "picosha2.h"

#include <chrono>
#include <iomanip>
#include <sstream>

#ifndef AUTOCOG_VERSION
#define AUTOCOG_VERSION "0.0.0"
#endif

namespace autocog {

using json = nlohmann::json;

/// SHA-256 of a string, truncated to 16 hex characters.
static std::string sha256_16(std::string const & input) {
    std::string hash_hex;
    picosha2::hash256_hex_string(input, hash_hex);
    return hash_hex.substr(0, 16);
}

/// RFC 3339 UTC timestamp (always 'Z').
static std::string utc_timestamp() {
    auto now = std::chrono::system_clock::now();
    auto time_t_now = std::chrono::system_clock::to_time_t(now);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()) % 1000;

    std::tm tm_utc{};
#if defined(_WIN32)
    gmtime_s(&tm_utc, &time_t_now);
#else
    gmtime_r(&time_t_now, &tm_utc);
#endif

    std::ostringstream ss;
    ss << std::put_time(&tm_utc, "%Y-%m-%dT%H:%M:%S");
    ss << '.' << std::setfill('0') << std::setw(3) << ms.count() << 'Z';
    return ss.str();
}

std::string compute_uid(json artifact) {
    // Strip metadata entirely before hashing — the UID represents
    // the structural content, not the lineage/timestamp metadata.
    // This ensures stability: same input → same UID across runs.
    artifact.erase("metadata");
    // nlohmann::json::dump() with sorted keys (default with std::map)
    // and no whitespace = JCS-compatible for our purposes
    return sha256_16(artifact.dump(-1));
}

std::string compute_source_uid(std::string const & content) {
    return sha256_16(content);
}

json build_metadata(std::string const & format,
                    std::string const & source_uid) {
    json meta;
    meta["format"] = format;
    meta["version"] = AUTOCOG_VERSION;
    meta["source_uid"] = source_uid;
    meta["timestamp"] = utc_timestamp();
    // uid is computed from the full artifact (with metadata minus uid)
    // Caller should use attach_metadata() instead for the circular computation.
    return meta;
}

void attach_metadata(json & artifact,
                     std::string const & format,
                     std::string const & source_uid) {
    json meta;
    meta["format"] = format;
    meta["version"] = AUTOCOG_VERSION;
    meta["source_uid"] = source_uid;
    meta["timestamp"] = utc_timestamp();
    // Set metadata without uid first
    artifact["metadata"] = meta;
    // Compute uid from the artifact (with metadata but without uid)
    meta["uid"] = compute_uid(artifact);
    artifact["metadata"] = meta;
}

} // namespace autocog
