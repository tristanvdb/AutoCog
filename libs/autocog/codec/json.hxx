#ifndef AUTOCOG_CODEC_JSON_HXX
#define AUTOCOG_CODEC_JSON_HXX

// Free-function JSON conversion for autocog::data artifacts.
//
// DRAFT layout -- for review. Conversion lives here, NOT on the types, so the
// artifact headers stay pure data (no <nlohmann/json.hpp> dependency). Only
// translation units that serialize/deserialize include this header.
// Implementations are split per type into data/<x>-json.cxx (dash, not dot).

#include "autocog/data/metadata.hxx"
#include "autocog/data/syntax.hxx"
#include "autocog/data/search.hxx"
#include "autocog/data/vocab.hxx"
#include "autocog/data/ftt.hxx"
#include "autocog/data/fta.hxx"
#include "autocog/data/sta.hxx"
#include "autocog/utilities/types.hxx"

#include "autocog/utilities/errors.hxx"

#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>

namespace autocog::codec {

// Run a codec reader body, converting any nlohmann JSON error (missing key,
// wrong type, bad index, ...) into std::invalid_argument. Through the Python
// bindings pybind maps that to ValueError instead of leaking a raw nlohmann
// exception as a bare RuntimeError; in tools it surfaces as a clean message.
// A schema check will eventually reject malformed input earlier with a
// friendlier, typed error -- until then this is the safety net. Typed autocog
// errors (e.g. SchemaError for an unknown discriminant) pass through untouched.
template <class F>
inline void read_guarded(char const * what, F && fill) {
    try {
        std::forward<F>(fill)();
    } catch (nlohmann::json::exception const & e) {
        throw std::invalid_argument(
            std::string("autocog::codec: malformed ") + what + ": " + e.what());
    }
}

// Serialize: deduces T from the value, returns a fresh DOM. Specialized per
// type in data/<x>-json.cxx; artifact-owned sub-structs get file-local
// specializations in the same translation unit.
template <class T> nlohmann::json to_json(T const & obj);

template <> nlohmann::json to_json(autocog::data::Metadata const &);
template <> nlohmann::json to_json(autocog::data::Syntax const &);
template <> nlohmann::json to_json(autocog::data::SearchConfig const &);
template <> nlohmann::json to_json(autocog::data::VocabExpr const &);   // shared by FTA and STA
template <> nlohmann::json to_json(autocog::data::FTT const &);
template <> nlohmann::json to_json(autocog::data::FTA const &);
template <> nlohmann::json to_json(autocog::data::STA const &);

// Shared sub-structs used across data translation units.
template <> nlohmann::json to_json(autocog::data::PathStep const &);
template <> nlohmann::json to_json(autocog::data::Channel const &);

// Deserialize workhorse: fills an existing object in place, so building a
// vector of sub-structs costs no per-element move
// (vec.emplace_back(); from_json(j, vec.back());). Specialized per type.
template <class T> void from_json(nlohmann::json const & dom, T & out);

template <> void from_json(nlohmann::json const &, autocog::data::Metadata &);
template <> void from_json(nlohmann::json const &, autocog::data::Syntax &);
template <> void from_json(nlohmann::json const &, autocog::data::SearchConfig &);
template <> void from_json(nlohmann::json const &, autocog::data::VocabExpr &);
template <> void from_json(nlohmann::json const &, autocog::data::FTT &);
template <> void from_json(nlohmann::json const &, autocog::data::FTA &);
template <> void from_json(nlohmann::json const &, autocog::data::STA &);

template <> void from_json(nlohmann::json const &, autocog::data::PathStep &);
template <> void from_json(nlohmann::json const &, autocog::data::Channel &);

template <> nlohmann::json to_json(autocog::types::Document const &);
template <> void from_json(nlohmann::json const &, autocog::types::Document &);

// Artifact boundary: construct, fill in place, finalize, return owned. Generic
// body -- instantiated only for artifacts (they have finalize()); sub-values
// like Metadata/VocabExpr use the two-arg fill above.
template <class T> std::unique_ptr<T> from_json(nlohmann::json const & dom) {
    auto out = std::make_unique<T>();
    from_json(dom, *out);
    out->finalize();
    return out;
}

// Text/file helpers, generic over the templates above.
template <class T> std::string        to_string(T const & obj) { return to_json(obj).dump(); }
template <class T> std::unique_ptr<T> from_string(std::string const & text) {
    return from_json<T>(nlohmann::json::parse(text));
}
template <class T> void               to_file(T const & obj, std::string const & path) {
    std::ofstream out(path);
    if (!out)
        throw autocog::FileError("autocog::data: cannot open '" + path + "' for writing", path);
    out << to_string(obj);
}
template <class T> std::unique_ptr<T> from_file(std::string const & path) {
    std::ifstream in(path);
    if (!in)
        throw autocog::FileError("autocog::data: cannot open '" + path + "' for reading", path);
    std::stringstream ss; ss << in.rdbuf();
    return from_string<T>(ss.str());
}

// Parse JSON from an argument that is either an existing file path or inline
// JSON text. Convenient for the dev/debug command-line tools, where small
// configs/content can be passed directly on the command line.
inline nlohmann::json json_from_file_or_string(std::string const & arg) {
    if (std::filesystem::exists(arg)) {
        std::ifstream in(arg);
        if (!in)
            throw autocog::FileError("autocog::data: cannot open '" + arg + "' for reading", arg);
        std::stringstream ss; ss << in.rdbuf();
        return nlohmann::json::parse(ss.str());
    }
    return nlohmann::json::parse(arg);
}

// Artifact variant of the above: load from a file path if it exists, otherwise
// parse the argument as inline JSON text. Returns an owned, finalized artifact.
template <class T> std::unique_ptr<T> from_file_or_string(std::string const & arg) {
    return from_json<T>(json_from_file_or_string(arg));
}

}

#endif // AUTOCOG_CODEC_JSON_HXX
