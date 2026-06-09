#ifndef AUTOCOG_UTILITIES_ERRORS_HXX
#define AUTOCOG_UTILITIES_ERRORS_HXX

#include "autocog/utilities/location.hxx"

#include <string>
#include <exception>
#include <optional>

namespace autocog {

// ============================================================================
// Root — single common ancestor for all AutoCog exceptions
// ============================================================================

struct AutoCogError : std::exception {
    std::string message;

    AutoCogError(std::string msg)
        : message(std::move(msg)) {}

    char const * what() const noexcept override { return message.c_str(); }
};

// ============================================================================
// Execution tree — errors during prompt instantiation, evaluation, parsing
// ============================================================================

struct ExecutionError : AutoCogError {
    using AutoCogError::AutoCogError;
};

struct ConfigError : ExecutionError {
    std::string path;
    ConfigError(std::string msg, std::string p)
        : ExecutionError(std::move(msg)), path(std::move(p)) {}
};

struct SchemaError : ExecutionError {
    std::string path;
    SchemaError(std::string msg, std::string p)
        : ExecutionError(std::move(msg)), path(std::move(p)) {}
};

struct IntegrityError : ExecutionError {
    std::string format;
    std::string expected;
    std::string actual;
    IntegrityError(std::string msg, std::string fmt, std::string e, std::string a)
        : ExecutionError(std::move(msg)),
          format(std::move(fmt)), expected(std::move(e)), actual(std::move(a)) {}
};

struct ModelError : ExecutionError {
    int model_id;
    std::string op;
    ModelError(std::string msg, int mid, std::string o)
        : ExecutionError(std::move(msg)), model_id(mid), op(std::move(o)) {}
};

struct OrchestrationError : ExecutionError {
    std::string prompt;
    std::string field;
    OrchestrationError(std::string msg, std::string p, std::string f = {})
        : ExecutionError(std::move(msg)), prompt(std::move(p)), field(std::move(f)) {}
};

struct FlowInvariantError : OrchestrationError {
    FlowInvariantError(std::string msg, std::string p, std::string choice)
        : OrchestrationError(std::move(msg), std::move(p), std::move(choice)) {}
};

struct FileError : ExecutionError {
    std::string path;
    FileError(std::string msg, std::string p)
        : ExecutionError(std::move(msg)), path(std::move(p)) {}
};

// Note: RemoteError and Timeout are intentionally Python-only (see
// modules/autocog/errors.py). They originate from the Python remote layer and
// are never thrown from C++, so there are no C++ structs (or translator
// catches) for them. Reintroduce here if a C++ remote/timeout path is added.

// ============================================================================
// Compilation — errors raised while compiling STL source
// ============================================================================

struct CompileError : AutoCogError {
    std::optional<autocog::location::SourceRange> location;
    CompileError(std::string msg, std::optional<autocog::location::SourceRange> loc = std::nullopt)
        : AutoCogError(std::move(msg)), location(std::move(loc)) {}
};

// ============================================================================
// NotImplementedError — temporary gaps, slated for removal before 1.0
// ============================================================================

struct NotImplementedError : AutoCogError {
    NotImplementedError(std::string msg)
        : AutoCogError(std::move(msg)) {}
};

}

#endif // AUTOCOG_UTILITIES_ERRORS_HXX
