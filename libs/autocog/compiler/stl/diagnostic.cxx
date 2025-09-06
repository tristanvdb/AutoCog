
#include "autocog/compiler/stl/diagnostic.hxx"

#include <sstream>

#include <stdexcept>

namespace autocog::compiler::stl {

Diagnostic::Diagnostic(DiagnosticLevel const level_, std::string message_) :
  level(level_),
  message(message_),
  source_line(std::nullopt),
  location(std::nullopt),
  notes()
{}

Diagnostic::Diagnostic(DiagnosticLevel const level_, std::string message_, std::string source_line_, SourceLocation location_) :
  level(level_),
  message(message_),
  source_line(source_line_),
  location(location_),
  notes()
{}

std::string Diagnostic::format(std::unordered_map<std::string, int> const & fileids) const {
    std::stringstream ss;
    
    // Main error message
    if (location) {
      auto const & loc = location.value();
      std::string filepath = "unknown";
      for (auto [fpath,fid]: fileids) {
        if (fid == loc.fid) {
          filepath = fpath;
          break;
        }
      }
      ss << filepath << ":"  << loc.line << ":" << loc.column << ": ";
    }
    switch (level) {
        case DiagnosticLevel::Error:   ss << "error: ";   break;
        case DiagnosticLevel::Warning: ss << "warning: "; break;
        case DiagnosticLevel::Note:    ss << "note: ";    break;
    }
    ss << message << "\n";
    
    // Source line with caret
    if (source_line) {
        ss << "  " << source_line.value() << "\n";
        if (location) {
          ss << "  " << std::string(location.value().column - 1, ' ') << "^\n";
        }
    }
    
    // Additional notes
    for (const auto& note : notes) {
        ss << "  note: " << note << "\n";
    }
    
    return ss.str();
}

}

