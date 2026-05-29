
#include "autocog/compiler/stl/driver.hxx"

#include <algorithm>
#include <vector>

namespace autocog::compiler::stl {

// ============================================================================
// Mangling
// ============================================================================

std::string Driver::mangle(std::string const & name, ir::VarMap const & bindings) const {
    if (bindings.empty()) {
        return name;
    }

    std::string result = name + "__";

    // Sort keys for deterministic mangling
    std::vector<std::string> keys;
    keys.reserve(bindings.size());
    for (const auto& [key, _] : bindings) {
        keys.push_back(key);
    }
    std::sort(keys.begin(), keys.end());

    // Append values in sorted key order
    bool first = true;
    for (const auto& key : keys) {
        if (!first) result += "_";
        first = false;

        const auto& value = bindings.at(key);

        std::visit([&result](auto&& val) {
            using T = std::decay_t<decltype(val)>;
            if constexpr (std::is_same_v<T, int>) {
                result += std::to_string(val);
            } else if constexpr (std::is_same_v<T, float>) {
                result += std::to_string(val);
            } else if constexpr (std::is_same_v<T, bool>) {
                result += val ? "true" : "false";
            } else if constexpr (std::is_same_v<T, std::string>) {
                auto hash = std::hash<std::string>{}(val);
                result += std::to_string(hash);
            } else if constexpr (std::is_same_v<T, std::nullptr_t>) {
                result += "null";
            }
        }, value);
    }

    return result;
}

// ============================================================================
// Diagnostics
// ============================================================================

void Driver::emit_error(std::string msg, std::optional<SourceRange> const & loc) {
    if (loc) {
        diagnostics.emplace_back(DiagnosticLevel::Error, msg, loc.value().start);
    } else {
        diagnostics.emplace_back(DiagnosticLevel::Error, msg);
    }
}

void Driver::emit_warning(std::string msg, std::optional<SourceRange> const & loc) {
    if (loc) {
        diagnostics.emplace_back(DiagnosticLevel::Warning, msg, loc.value().start);
    } else {
        diagnostics.emplace_back(DiagnosticLevel::Warning, msg);
    }
}

void Driver::emit_note(std::string msg, std::optional<SourceRange> const & loc) {
    if (loc) {
        diagnostics.emplace_back(DiagnosticLevel::Note, msg, loc.value().start);
    } else {
        diagnostics.emplace_back(DiagnosticLevel::Note, msg);
    }
}

bool Driver::report_errors() {
    for (auto const & diag : diagnostics) {
        if (print_diagnostics) {
            std::cerr << diag.format(fileids) << std::endl;
        }
        switch (diag.level) {
            case DiagnosticLevel::Error:   errors++;   break;
            case DiagnosticLevel::Warning: warnings++; break;
            case DiagnosticLevel::Note:    notes++;    break;
        }
    }

    if (print_diagnostics) {
        if (errors > 0) {
            std::cerr << "Failed with " << errors << " error(s), "
                      << warnings << " warning(s), and " << notes << " note(s).\n";
        } else if (warnings > 0) {
            std::cerr << "Passed with " << warnings << " warning(s) and " << notes << " note(s).\n";
        } else if (notes > 0) {
            std::cerr << "Passed with " << notes << " note(s).\n";
        }
    }

    // Retain diagnostics for post-compile retrieval (e.g. the Python binding),
    // then clear the per-stage buffer so the next stage starts fresh.
    collected.splice(collected.end(), diagnostics);

    return errors > 0;
}

// ============================================================================
// Utilities
// ============================================================================

std::optional<int> Driver::fileid(std::string const & filename) const {
    auto it = fileids.find(filename);
    if (it != fileids.end()) {
        return it->second;
    }
    return std::nullopt;
}

// ============================================================================
// Compilation orchestration
// ============================================================================

std::optional<int> Driver::compile_stages() {
    // Stage 1: Parse
    if (auto e = run_parse()) return e;
    if (stage == CompilationStage::Parse) return std::nullopt;

    // Stage 2: Collect symbols
    if (auto e = run_symbols()) return e;
    if (stage == CompilationStage::Symbols) return std::nullopt;

    // Stage 3: Evaluate globals
    if (auto e = run_globals()) return e;
    if (stage == CompilationStage::Globals) return std::nullopt;

    // Stage 4: Build instantiation graph
    if (auto e = run_instantiate()) return e;
    if (stage == CompilationStage::Instantiate) return std::nullopt;

    // Stage 5: Assemble IR
    if (auto e = run_assemble()) return e;
    if (stage == CompilationStage::Assemble) return std::nullopt;

    // Stage 6: Generate STA
    if (auto e = run_generate()) return e;

    return std::nullopt;
}

std::optional<int> Driver::compile() {
    return compile_stages();
}

} // namespace autocog::compiler::stl
