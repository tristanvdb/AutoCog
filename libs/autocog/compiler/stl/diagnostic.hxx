#ifndef AUTOCOG_COMPILER_STL_DIAGNOSTIC_HXX
#define AUTOCOG_COMPILER_STL_DIAGNOSTIC_HXX

#include "autocog/compiler/stl/location.hxx"

#include <string>
#include <vector>
#include <unordered_map>
#include <optional>

namespace autocog::compiler::stl {

enum class DiagnosticLevel { Error, Warning, Note };

struct Diagnostic {
  DiagnosticLevel const level;
  std::string message;
  std::optional<std::string> source_line;
  std::optional<SourceLocation> location;
  std::vector<std::string> notes;

  Diagnostic(DiagnosticLevel const level_, std::string message_);
  Diagnostic(DiagnosticLevel const level_, std::string message_, SourceLocation location_);
  Diagnostic(DiagnosticLevel const level_, std::string message_, std::string source_line_, SourceLocation location_);

  std::string format(std::unordered_map<std::string, int> const & fileids) const;
};

struct CompileError : std::exception {
  std::string message;
  std::optional<SourceRange> location;
  
  CompileError(std::string msg, std::optional<SourceRange> loc = std::nullopt);
  
  const char * what() const noexcept override;
};

}

#endif /* AUTOCOG_COMPILER_STL_DIAGNOSTIC_HXX */
