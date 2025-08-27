#ifndef AUTOCOG_PARSER_STL_DIAGNOSTIC_HXX
#define AUTOCOG_PARSER_STL_DIAGNOSTIC_HXX

#include "autocog/parser/stl/location.hxx"

#include <string>
#include <vector>
#include <optional>

namespace autocog::parser {

enum class DiagnosticLevel { Error, Warning, Note };

struct Diagnostic {
  DiagnosticLevel const level;
  std::string message;
  std::optional<std::string> source_line;
  std::optional<SourceLocation> location;
  std::vector<std::string> notes;

  Diagnostic(DiagnosticLevel const level_, std::string message_);
  Diagnostic(DiagnosticLevel const level_, std::string message_, std::string source_line_, SourceLocation location_);

  std::string format() const;
};

}

#endif /* AUTOCOG_PARSER_STL_DIAGNOSTIC_HXX */
