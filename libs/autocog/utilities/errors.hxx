#ifndef AUTOCOG_UTILITIES_ERRORS_HXX
#define AUTOCOG_UTILITIES_ERRORS_HXX

#include <string>
#include <exception>

namespace autocog {

// ============================================================================
// Root — single common ancestor for all AutoCog exceptions
// ============================================================================

struct AutoCogError : std::exception {
    std::string message;
    bool recoverable;

    AutoCogError(std::string msg, bool rec = false)
        : message(std::move(msg)), recoverable(rec) {}

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
    ConfigError(std::string msg, std::string p, bool rec = false)
        : ExecutionError(std::move(msg), rec), path(std::move(p)) {}
};

struct ModelError : ExecutionError {
    int model_id;
    std::string op;
    ModelError(std::string msg, int mid, std::string o, bool rec = false)
        : ExecutionError(std::move(msg), rec), model_id(mid), op(std::move(o)) {}
};

struct OrchestrationError : ExecutionError {
    std::string prompt;
    std::string field;
    OrchestrationError(std::string msg, std::string p, std::string f = {}, bool rec = true)
        : ExecutionError(std::move(msg), rec), prompt(std::move(p)), field(std::move(f)) {}
};

struct FlowInvariantError : OrchestrationError {
    FlowInvariantError(std::string msg, std::string p, std::string choice)
        : OrchestrationError(std::move(msg), std::move(p), std::move(choice), /*rec=*/false) {}
};

struct RemoteError : ExecutionError {
    std::string endpoint;
    RemoteError(std::string msg, std::string ep, bool rec = true)
        : ExecutionError(std::move(msg), rec), endpoint(std::move(ep)) {}
};

struct Timeout : ExecutionError {
    float seconds;
    Timeout(std::string msg, float s, bool rec = true)
        : ExecutionError(std::move(msg), rec), seconds(s) {}
};

struct FileError : ExecutionError {
    std::string path;
    FileError(std::string msg, std::string p, bool rec = false)
        : ExecutionError(std::move(msg), rec), path(std::move(p)) {}
};

// ============================================================================
// NotImplementedError — temporary gaps, slated for removal before 1.0
// ============================================================================

struct NotImplementedError : AutoCogError {
    NotImplementedError(std::string msg)
        : AutoCogError(std::move(msg), false) {}
};

}

#endif // AUTOCOG_UTILITIES_ERRORS_HXX
