#ifndef AUTOCOG_RUNTIME_STA_SYNTAX_HXX
#define AUTOCOG_RUNTIME_STA_SYNTAX_HXX

#include <optional>
#include <string>

namespace autocog::runtime::sta {

// Syntax description for STA instantiation.
// Always loaded from a JSON file — no hardcoded presets.

struct Syntax {
    // All fields must be specified in the syntax JSON file.
    // No defaults — the syntax file is the single source of truth.

    // System message
    std::string system_msg;

    // Chat template wrapping (around system message + header)
    std::string header_pre;
    std::string header_mid;
    std::string header_post;

    // Header content
    std::string header_mechanic;
    std::string header_formats;
    std::string format_listing;

    // Field formatting
    std::string prompt_indent;
    std::string field_separator;
    std::string field_suffix;

    // Field description formatting
    std::string desc_pre;
    std::string desc_post;
    bool desc_inline;

    // Indexing and label content
    bool prompt_with_format;
    bool prompt_with_index;
    bool prompt_zero_index;

    // Format descriptions in header
    bool detailed_formats;

    // Stop token for completions (as text, tokenized by backend)
    std::string completion_stop;
};

// Load a syntax description from a JSON file.
// Only fields present in the JSON are overridden; others keep defaults.
Syntax load_syntax(std::string const & path);

}

#endif // AUTOCOG_RUNTIME_STA_SYNTAX_HXX
