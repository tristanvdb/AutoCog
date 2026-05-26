
#include "autocog/runtime/sta/load.hxx"
#include "autocog/runtime/sta/parse.hxx"

#include <nlohmann/json.hpp>

#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

using json = nlohmann::json;
namespace sta = autocog::runtime::sta;

static void print_usage(char const * prog) {
    std::cerr << "Usage: " << prog << " [options] <sta.json>\n\n"
              << "Parse STA-formatted text back to field values\n\n"
              << "Options:\n"
              << "  -p, --prompt <name>    Prompt name (default: entry point)\n"
              << "  -s, --syntax <file>    Syntax description JSON\n"
              << "  -i, --input <file>     Text to parse (default: stdin)\n"
              << "  -o, --output <file>    Output JSON (default: stdout)\n"
              << "  -h, --help             Show this help\n";
}

int main(int argc, char ** argv) {
    std::string sta_file, prompt_name, syntax_file, input_file, output_file;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "-h" || arg == "--help") { print_usage(argv[0]); return 0; }
        if ((arg == "-p" || arg == "--prompt") && i + 1 < argc) { prompt_name = argv[++i]; continue; }
        if ((arg == "-s" || arg == "--syntax") && i + 1 < argc) { syntax_file = argv[++i]; continue; }
        if ((arg == "-i" || arg == "--input") && i + 1 < argc) { input_file = argv[++i]; continue; }
        if ((arg == "-o" || arg == "--output") && i + 1 < argc) { output_file = argv[++i]; continue; }
        if (arg[0] == '-') { std::cerr << "Unknown option: " << arg << "\n"; return 1; }
        sta_file = arg;
    }

    if (sta_file.empty()) { std::cerr << "Error: no STA file specified\n"; print_usage(argv[0]); return 1; }

    // Load STA
    json sta_json;
    {
        std::ifstream f(sta_file);
        if (!f) { std::cerr << "Error: cannot read " << sta_file << "\n"; return 1; }
        sta_json = json::parse(f);
    }

    // Resolve prompt name
    if (prompt_name.empty()) {
        auto const & entries = sta_json["entry_points"];
        if (entries.empty()) { std::cerr << "Error: no entry points in STA\n"; return 1; }
        prompt_name = entries.begin().value().get<std::string>();
    }

    if (!sta_json["prompts"].contains(prompt_name)) {
        std::cerr << "Error: prompt '" << prompt_name << "' not found in STA\n";
        return 1;
    }

    auto prompt = sta::load_prompt(sta_json["prompts"][prompt_name]);

    // Load syntax
    sta::Syntax syntax;
    if (!syntax_file.empty()) {
        syntax = sta::load_syntax(syntax_file);
    }

    // Read input text
    std::string text;
    if (!input_file.empty()) {
        std::ifstream f(input_file);
        if (!f) { std::cerr << "Error: cannot read " << input_file << "\n"; return 1; }
        std::ostringstream ss;
        ss << f.rdbuf();
        text = ss.str();
    } else {
        std::ostringstream ss;
        ss << std::cin.rdbuf();
        text = ss.str();
    }

    // Parse
    auto result = sta::parse_text(prompt, syntax, text);
    json output = sta::field_record_to_json(result);

    // Output
    std::ostream * out = &std::cout;
    std::ofstream outfile;
    if (!output_file.empty()) {
        outfile.open(output_file);
        out = &outfile;
    }
    *out << output.dump(2) << std::endl;

    return 0;
}
