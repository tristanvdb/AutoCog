#ifndef AUTOCOG_RUNTIME_STA_SYNTAX_HXX
#define AUTOCOG_RUNTIME_STA_SYNTAX_HXX

#include <optional>
#include <string>

namespace autocog::runtime::sta {

// Syntax description for STA instantiation.
// Always loaded from a JSON file — no hardcoded presets.

struct Syntax {
    // System message
    std::string system_msg = "You are an AI expert interacting with your environment using a set of interactive questionnaires. A newline ends each statement (or prompt).";

    // Chat template wrapping (around system message + header)
    std::string header_pre;       // before system message
    std::string header_mid = "\n";  // between system message and field header
    std::string header_post = "\n"; // after field header, before "start:"

    // Header content
    std::string header_mechanic = "You are using the following syntax:";
    std::string header_formats  = "It includes the following named formats:";
    std::string format_listing  = "- ";

    // Field formatting
    std::string prompt_indent = "\t";
    std::string field_separator = "\n";
    std::string field_suffix = ":";

    // Indexing
    bool prompt_with_format = false;   // show format type in label
    bool prompt_with_index  = false;   // show array index in label
    bool prompt_zero_index  = false;   // 0-based or 1-based for select choices

    // Format descriptions in header
    bool detailed_formats = false;

    // Stop token for completions (as text, tokenized by backend)
    std::string completion_stop = "\n";
};

// Load a syntax description from a JSON file.
// Only fields present in the JSON are overridden; others keep defaults.
Syntax load_syntax(std::string const & path);

}

#endif // AUTOCOG_RUNTIME_STA_SYNTAX_HXX
