
#include "autocog/runtime/sta/load.hxx"
#include "autocog/runtime/sta/instantiate.hxx"

#include <nlohmann/json.hpp>

#include <fstream>
#include <iostream>
#include <string>

using json = nlohmann::json;
namespace sta = autocog::runtime::sta;

static void print_usage(char const * prog) {
    std::cerr << "Usage: " << prog << " [options] <sta.json>\n\n"
              << "Instantiate an STA prompt into an FTA\n\n"
              << "Options:\n"
              << "  -p, --prompt <name>    Prompt name (default: entry point)\n"
              << "  -s, --syntax <file>    Syntax description JSON\n"
              << "  -d, --data <file>      Initial content JSON (default: {})\n"
              << "  -o, --output <file>    Output FTA JSON (default: stdout)\n"
              << "  -t, --text             Print FTA as text instead of JSON\n"
              << "  -h, --help             Show this help\n";
}

int main(int argc, char ** argv) {
    std::string sta_file, prompt_name, syntax_file, data_file, output_file;
    bool text_mode = false;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "-h" || arg == "--help") { print_usage(argv[0]); return 0; }
        if (arg == "-t" || arg == "--text") { text_mode = true; continue; }
        if ((arg == "-p" || arg == "--prompt") && i + 1 < argc) { prompt_name = argv[++i]; continue; }
        if ((arg == "-s" || arg == "--syntax") && i + 1 < argc) { syntax_file = argv[++i]; continue; }
        if ((arg == "-d" || arg == "--data") && i + 1 < argc) { data_file = argv[++i]; continue; }
        if ((arg == "-o" || arg == "--output") && i + 1 < argc) { output_file = argv[++i]; continue; }
        if (arg[0] == '-') { std::cerr << "Unknown option: " << arg << "\n"; return 1; }
        sta_file = arg;
    }

    if (sta_file.empty()) { std::cerr << "Error: no STA file specified\n"; print_usage(argv[0]); return 1; }

    // Load STA program
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
        prompt_name = entries.begin().value()["prompt"].get<std::string>();
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

    // Load initial content
    json content = json::object();
    if (!data_file.empty()) {
        std::ifstream f(data_file);
        if (!f) { std::cerr << "Error: cannot read " << data_file << "\n"; return 1; }
        content = json::parse(f);
    }

    // Instantiate
    auto fta = sta::instantiate(prompt, content, syntax);

    // Output
    if (text_mode) {
        std::cout << sta::render_text(fta) << "\n";
    } else {
        std::ostream * out = &std::cout;
        std::ofstream outfile;
        if (!output_file.empty()) {
            outfile.open(output_file);
            out = &outfile;
        }
        *out << fta.dump(2) << std::endl;
    }

    return 0;
}
