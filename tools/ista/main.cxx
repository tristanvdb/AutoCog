
#include "autocog/runtime/sta/instantiate.hxx"
#include "autocog/codec/json.hxx"
#include "autocog/data/sta.hxx"
#include "autocog/data/syntax.hxx"
#include "autocog/data/search.hxx"
#include "autocog/utilities/types.hxx"
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
namespace sta   = autocog::runtime::sta;
namespace data  = autocog::data;
namespace codec = autocog::codec;

static void print_usage(char const * prog) {
    std::cerr << "Usage: " << prog << " --sta <file> --prompt <name> --syntax <file|json>\n"
              << "            --search <file|json> --content <file|json> --fta <file>\n\n"
              << "Instantiate an STA prompt into an FTA.\n\n"
              << "Options (all inputs and --fta are required):\n"
              << "  --sta <file>           Compiled STA JSON\n"
              << "  --prompt <name>        Prompt to instantiate\n"
              << "  --syntax <file|json>   Syntax config: a file path or inline JSON\n"
              << "  --search <file|json>   Search config: a file path or inline JSON\n"
              << "  --content <file|json>  Initial content: a file path or inline JSON\n"
              << "  --fta <file>           Output FTA JSON (/dev/stdout for stdout)\n"
              << "  --verbose [LEVEL]      Log level (trace,debug,info,warn,error; default: debug)\n"
              << "  --version              Show version\n"
              << "  --build-info           Show build configuration\n"
              << "  --help                 Show this help\n";
}

static int run(int argc, char ** argv) {
    std::string sta_file, prompt_name, syntax_arg, search_arg, content_arg, fta_file;

    autocog::init_console_logger();

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--help") { print_usage(argv[0]); return 0; }
        if (arg == "--version") { std::cout << "ista " << autocog::version() << "\n"; return 0; }
        if (arg == "--build-info") { std::cout << autocog::build_info(); return 0; }
        if (arg == "--prompt"  && i + 1 < argc) { prompt_name = argv[++i]; continue; }
        if (arg == "--sta"     && i + 1 < argc) { sta_file    = argv[++i]; continue; }
        if (arg == "--syntax"  && i + 1 < argc) { syntax_arg  = argv[++i]; continue; }
        if (arg == "--search"  && i + 1 < argc) { search_arg  = argv[++i]; continue; }
        if (arg == "--content" && i + 1 < argc) { content_arg = argv[++i]; continue; }
        if (arg == "--fta"     && i + 1 < argc) { fta_file    = argv[++i]; continue; }
        if (arg == "--verbose") {
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
        std::cerr << "Unknown option: " << arg << "\n"; print_usage(argv[0]); return 1;
    }

    if (sta_file.empty())    { std::cerr << "Error: --sta is required\n";     print_usage(argv[0]); return 1; }
    if (prompt_name.empty()) { std::cerr << "Error: --prompt is required\n";  print_usage(argv[0]); return 1; }
    if (syntax_arg.empty())  { std::cerr << "Error: --syntax is required\n";  print_usage(argv[0]); return 1; }
    if (search_arg.empty())  { std::cerr << "Error: --search is required\n";  print_usage(argv[0]); return 1; }
    if (content_arg.empty()) { std::cerr << "Error: --content is required\n"; print_usage(argv[0]); return 1; }
    if (fta_file.empty())    { std::cerr << "Error: --fta is required\n";     print_usage(argv[0]); return 1; }

    // Load STA program (file path).
    if (!std::filesystem::exists(sta_file)) {
        throw autocog::FileError("Cannot find file: " + sta_file, sta_file);
    }
    auto program = codec::from_file<data::STA>(sta_file);

    auto pit = program->prompts.find(prompt_name);
    if (pit == program->prompts.end()) {
        std::cerr << "Error: prompt '" << prompt_name << "' not found in STA\n";
        return 1;
    }

    // Syntax, search, and content each accept a file path or inline JSON.
    auto syntax = codec::from_file_or_string<data::Syntax>(syntax_arg);
    auto search = codec::from_file_or_string<data::SearchConfig>(search_arg);
    autocog::types::Document content{ autocog::types::Document::Object{} };
    codec::from_json(codec::json_from_file_or_string(content_arg), content);

    // Instantiate. The FTA's sta-provenance is the loaded STA's content hash,
    // carried verbatim from the finalized STA we just loaded.
    std::string sta_uid = program->metadata ? program->metadata->hash : std::string{};
    auto fta = sta::instantiate(pit->second, content, *syntax, *search, sta_uid);

    // Write FTA JSON to --fta (/dev/stdout for stdout).
    std::ostream * out = &std::cout;
    std::ofstream outfile;
    if (fta_file != "/dev/stdout") {
        outfile.open(fta_file);
        if (!outfile) { std::cerr << "Error: cannot write " << fta_file << "\n"; return 1; }
        out = &outfile;
    }
    *out << codec::to_json(fta).dump(2) << std::endl;

    return 0;
}

int main(int argc, char ** argv) {
    return autocog::utilities::guard_main([&]{ return run(argc, argv); });
}
