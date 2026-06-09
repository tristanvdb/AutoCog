#ifndef AUTOCOG_DATA_DIAGNOSTIC_HXX
#define AUTOCOG_DATA_DIAGNOSTIC_HXX

#include "autocog/utilities/location.hxx"

#include <optional>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

namespace autocog::data {

enum class DiagnosticLevel { Error, Warning, Note };

/// A compile diagnostic: a level, a message, and an optional source location
/// (and the source line, for a caret). Pure data, header-only; the compiler
/// produces these and the binding serializes them. Not a stored artifact.
struct Diagnostic {
  DiagnosticLevel const level;
  std::string message;
  std::optional<std::string> source_line;
  std::optional<autocog::location::SourceLocation> location;
  std::vector<std::string> notes;

  Diagnostic(DiagnosticLevel const level_, std::string message_)
    : level(level_), message(std::move(message_)) {}
  Diagnostic(DiagnosticLevel const level_, std::string message_,
             autocog::location::SourceLocation location_)
    : level(level_), message(std::move(message_)), location(location_) {}
  Diagnostic(DiagnosticLevel const level_, std::string message_,
             std::string source_line_, autocog::location::SourceLocation location_)
    : level(level_), message(std::move(message_)),
      source_line(std::move(source_line_)), location(location_) {}

  /// Render "file:line:col: level: message" (clang-style), resolving the
  /// location's fid against the given per-compilation file table.
  std::string format(std::unordered_map<std::string, int> const & fileids) const {
    std::stringstream ss;
    if (location) {
      auto const & loc = *location;
      std::string filepath = "unknown";
      for (auto const & [fpath, fid] : fileids) {
        if (fid == loc.fid) { filepath = fpath; break; }
      }
      ss << filepath << ":" << loc.line << ":" << loc.column << ": ";
    }
    switch (level) {
      case DiagnosticLevel::Error:   ss << "error: ";   break;
      case DiagnosticLevel::Warning: ss << "warning: "; break;
      case DiagnosticLevel::Note:    ss << "note: ";    break;
    }
    ss << message << "\n";
    if (source_line) {
      ss << "  " << *source_line << "\n";
      if (location) ss << "  " << std::string(location->column - 1, ' ') << "^\n";
    }
    for (auto const & note : notes) ss << "  note: " << note << "\n";
    return ss.str();
  }
};

}

#endif // AUTOCOG_DATA_DIAGNOSTIC_HXX
