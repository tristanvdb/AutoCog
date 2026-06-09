
#include "autocog/runtime/sta/walk.hxx"
#include "autocog/codec/json.hxx"
#include "autocog/data/sta.hxx"
#include "autocog/data/ftt.hxx"
#include "autocog/utilities/types.hxx"
#include "autocog/build_info.hxx"
#include "autocog/logging.hxx"
#include "autocog/utilities/errors.hxx"
#include "autocog/utilities/exception.hxx"
#include <algorithm>
#include <filesystem>
#include <memory>

#include <nlohmann/json.hpp>

#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

using json = nlohmann::json;
namespace sta   = autocog::runtime::sta;
namespace data  = autocog::data;
namespace codec = autocog::codec;

static void print_usage(char const * prog) {
    std::cerr << "Usage: " << prog << " --sta <file> --ftt <file> --prompt <name> --frame <file>\n\n"
              << "Score an FTT and parse it into a frame of field values.\n\n"
              << "Options (--sta, --ftt, --prompt, --frame are required):\n"
              << "  --sta <file>           Compiled STA JSON\n"
              << "  --ftt <file>           Input FTT JSON\n"
              << "  --prompt <name>        Prompt the FTT corresponds to\n"
              << "  --frame <file>         Output frame JSON (/dev/stdout for stdout)\n"
              << "  --metric <name>        Scoring metric: best (default)\n"
              << "  --verbose [LEVEL]      Log level (trace,debug,info,warn,error; default: debug)\n"
              << "  --version              Show version\n"
              << "  --build-info           Show build configuration\n"
              << "  --help                 Show this help\n";
}

static int run(int argc, char ** argv) {
    std::string sta_file, prompt_name, ftt_file, frame_file;
    std::string metric = "best";

    autocog::init_console_logger();

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--help") { print_usage(argv[0]); return 0; }
        if (arg == "--version") { std::cout << "psta " << autocog::version() << "\n"; return 0; }
        if (arg == "--build-info") { std::cout << autocog::build_info(); return 0; }
        if (arg == "--sta"    && i + 1 < argc) { sta_file    = argv[++i]; continue; }
        if (arg == "--ftt"    && i + 1 < argc) { ftt_file    = argv[++i]; continue; }
        if (arg == "--prompt" && i + 1 < argc) { prompt_name = argv[++i]; continue; }
        if (arg == "--frame"  && i + 1 < argc) { frame_file  = argv[++i]; continue; }
        if (arg == "--metric" && i + 1 < argc) { metric      = argv[++i]; continue; }
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

    if (sta_file.empty())    { std::cerr << "Error: --sta is required\n";    print_usage(argv[0]); return 1; }
    if (ftt_file.empty())    { std::cerr << "Error: --ftt is required\n";    print_usage(argv[0]); return 1; }
    if (prompt_name.empty()) { std::cerr << "Error: --prompt is required\n"; print_usage(argv[0]); return 1; }
    if (frame_file.empty())  { std::cerr << "Error: --frame is required\n";  print_usage(argv[0]); return 1; }

    // Load STA
    if (!std::filesystem::exists(sta_file)) {
        throw autocog::FileError("Cannot find file: " + sta_file, sta_file);
    }
    auto program = codec::from_file<data::STA>(sta_file);

    if (program->prompts.find(prompt_name) == program->prompts.end()) {
        std::cerr << "Error: prompt '" << prompt_name << "' not found in STA\n";
        return 1;
    }

    // Read FTT (file path).
    auto ftt = codec::from_file<data::FTT>(ftt_file);

    // Build the frame via the canonical FTT->frame walker. psta has no input
    // content and historically kept raw select indices, so select-resolution
    // is disabled here (resolve_selects=false), preserving its output. The
    // --metric selects which completed path becomes the frame.
    autocog::types::Document empty{ autocog::types::Document::Object{} };
    auto result = sta::walk_ftt_to_frame(*ftt, *program, prompt_name,
                                         empty, /*resolve_selects=*/false, metric);

    // Write frame JSON to --frame (/dev/stdout for stdout).
    std::ostream * out = &std::cout;
    std::ofstream outfile;
    if (frame_file != "/dev/stdout") {
        outfile.open(frame_file);
        if (!outfile) { std::cerr << "Error: cannot write " << frame_file << "\n"; return 1; }
        out = &outfile;
    }
    *out << codec::to_json(result).dump(2) << std::endl;

    return 0;
}

int main(int argc, char ** argv) {
    return autocog::utilities::guard_main([&]{ return run(argc, argv); });
}
