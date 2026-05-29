
#include "autocog/runtime/sta/load.hxx"
#include "autocog/runtime/sta/instantiate.hxx"
#include "autocog/runtime/sta/search.hxx"
#include "autocog/build_info.hxx"
#include "autocog/logging.hxx"
#include "autocog/utilities/errors.hxx"
#include "autocog/utilities/exception.hxx"
#include <algorithm>
#include <filesystem>

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
              << "  -s, --syntax <file>    Syntax description JSON (required)\n"
              << "  --search <file>        Search config JSON (required)\n"
              << "  -d, --data <file>      Initial content JSON (default: {})\n"
              << "  -o, --output <file>    Output FTA JSON (default: stdout)\n"
              << "  -t, --text             Print FTA as text instead of JSON\n"
              << "  -V, --verbose [LEVEL]  Log level (trace,debug,info,warn,error; default: debug)\n"
              << "  -v, --version          Show version\n"
              << "  --build-info           Show build configuration\n"
              << "  -h, --help             Show this help\n";
}

static int run(int argc, char ** argv) {
    std::string sta_file, prompt_name, syntax_file, search_file, data_file, output_file;
    bool text_mode = false;

    autocog::init_console_logger();

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "-h" || arg == "--help") { print_usage(argv[0]); return 0; }
        if (arg == "-v" || arg == "--version") { std::cout << "ista " << autocog::version() << "\n"; return 0; }
        if (arg == "--build-info") { std::cout << autocog::build_info(); return 0; }
        if (arg == "-t" || arg == "--text") { text_mode = true; continue; }
        if ((arg == "-p" || arg == "--prompt") && i + 1 < argc) { prompt_name = argv[++i]; continue; }
        if ((arg == "-s" || arg == "--syntax") && i + 1 < argc) { syntax_file = argv[++i]; continue; }
        if (arg == "--search" && i + 1 < argc) { search_file = argv[++i]; continue; }
        if ((arg == "-d" || arg == "--data") && i + 1 < argc) { data_file = argv[++i]; continue; }
        if ((arg == "-o" || arg == "--output") && i + 1 < argc) { output_file = argv[++i]; continue; }
        if (arg == "-V" || arg == "--verbose") {
          spdlog::level::level_enum lvl = spdlog::level::debug;
          if (i + 1 < argc && autocog::looks_like_level_token(argv[i + 1])) {
            if (autocog::parse_level(argv[i + 1], lvl)) {
              ++i;
            } else {
              std::cerr << "Error: unknown verbosity level '" << argv[i + 1]
                        << "' (expected: trace, debug, info, warn, error, critical, off)\n";
              return 1;
            }
          }
          autocog::init_console_logger(lvl);
          continue;
        }
        if (arg[0] == '-') { std::cerr << "Unknown option: " << arg << "\n"; return 1; }
        sta_file = arg;
    }

    if (sta_file.empty()) { std::cerr << "Error: no STA file specified\n"; print_usage(argv[0]); return 1; }
    if (syntax_file.empty()) { std::cerr << "Error: --syntax is required\n"; return 1; }
    if (search_file.empty()) { std::cerr << "Error: --search is required\n"; return 1; }

    // Load STA program
    json sta_json;
    {
        if (!std::filesystem::exists(sta_file)) {
            throw autocog::FileError("Cannot find file: " + sta_file, sta_file);
        }
        std::ifstream f(sta_file);
        if (!f) { throw autocog::FileError("Cannot read file: " + sta_file, sta_file); }
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

    // Load syntax (required)
    sta::Syntax syntax = sta::load_syntax(syntax_file);

    // Load search config (required)
    sta::SearchConfig search = sta::load_search_config(search_file);

    // Load initial content
    json content = json::object();
    if (!data_file.empty()) {
        std::ifstream f(data_file);
        if (!f) { std::cerr << "Error: cannot read " << data_file << "\n"; return 1; }
        content = json::parse(f);
    }

    // Instantiate
    auto fta = sta::instantiate(prompt, content, syntax, search);

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

int main(int argc, char ** argv) {
    return autocog::utilities::guard_main([&]{ return run(argc, argv); });
}
