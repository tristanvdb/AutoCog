#include "autocog/runtime/sta/encode.hxx"
#include "autocog/backend/llama/manager.hxx"
#include "autocog/backend/llama/prepared.hxx"
#include "autocog/codec/json.hxx"
#include "autocog/data/fta.hxx"
#include "autocog/data/ftt.hxx"
#include "autocog/data/sta.hxx"
#include "autocog/utilities/types.hxx"
#include "autocog/build_info.hxx"
#include "autocog/logging.hxx"
#include "autocog/utilities/errors.hxx"
#include "autocog/utilities/exception.hxx"

#include <nlohmann/json.hpp>

#include <fstream>
#include <iostream>
#include <string>

namespace sta   = autocog::runtime::sta;
namespace llama = autocog::backend::llama;
namespace data  = autocog::data;
namespace codec = autocog::codec;

static void print_usage(char const * prog) {
    std::cerr << "Usage: " << prog << " --sta <file> --fta <file> --prompt <name> --frame <file|json>\n"
              << "            (--model <file> | --rng) --ftt <file>\n\n"
              << "Encode a frame back into the FTT of the path that produces it (the\n"
              << "inverse of psta). The FTA fixes the rendering syntax; the model fixes\n"
              << "the tokenizer. Re-rendering a recorded frame under another syntax is\n"
              << "re-running ista with that syntax and encoding against its FTA.\n\n"
              << "Options:\n"
              << "  --sta <file>           Compiled STA JSON (field table)\n"
              << "  --fta <file>           Instantiated FTA JSON (the target syntax)\n"
              << "  --prompt <name>        Prompt the frame corresponds to\n"
              << "  --frame <file|json>    Frame of field values: a file path or inline JSON\n"
              << "  --content <file|json>  Input content; resolves select values back to\n"
              << "                         indices when the frame holds resolved values\n"
              << "  --model <file>         GGUF model file (tokenizer)\n"
              << "  --rng                  Built-in RNG model (byte-level tokenizer)\n"
              << "  --score                Score the encoded path against the model:\n"
              << "                         every node's logprobs become P(token | prefix),\n"
              << "                         measuring constraint friction on forced tokens\n"
              << "  --ftt <file>           Output FTT JSON (/dev/stdout for stdout)\n"
              << "  --ctx N                Maximum context size for the model\n"
              << "  --batch <file>         Manifest mode: a JSON array of jobs\n"
              << "                         [{\"sta\":..,\"fta\":..,\"prompt\":..,\"frame\":..,\"ftt\":..}, ...]\n"
              << "                         sharing one loaded model (replaces the per-job flags)\n"
              << "  --verbose [LEVEL]      Log level (trace,debug,info,warn,error; default: debug)\n"
              << "  --version              Show version\n"
              << "  --build-info           Show build configuration\n"
              << "  --help                 Show this help\n";
}

static int run(int argc, char ** argv) {
    std::string sta_file, fta_file, prompt_name, frame_arg, content_arg, model_path, ftt_file, batch_file;
    unsigned ctx_size = 4096;
    bool use_rng = false;
    bool do_score = false;

    autocog::init_console_logger();

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--help") { print_usage(argv[0]); return 0; }
        if (arg == "--version") { std::cout << "efta " << autocog::version() << "\n"; return 0; }
        if (arg == "--build-info") { std::cout << autocog::build_info(); return 0; }
        if (arg == "--rng") { use_rng = true; continue; }
        if (arg == "--score") { do_score = true; continue; }
        if (arg == "--sta"    && i + 1 < argc) { sta_file    = argv[++i]; continue; }
        if (arg == "--fta"    && i + 1 < argc) { fta_file    = argv[++i]; continue; }
        if (arg == "--prompt" && i + 1 < argc) { prompt_name = argv[++i]; continue; }
        if (arg == "--frame"  && i + 1 < argc) { frame_arg   = argv[++i]; continue; }
        if (arg == "--content" && i + 1 < argc) { content_arg = argv[++i]; continue; }
        if (arg == "--model"  && i + 1 < argc) { model_path  = argv[++i]; continue; }
        if (arg == "--ftt"    && i + 1 < argc) { ftt_file    = argv[++i]; continue; }
        if (arg == "--ctx"    && i + 1 < argc) { ctx_size    = std::stoi(argv[++i]); continue; }
        if (arg == "--batch"  && i + 1 < argc) { batch_file  = argv[++i]; continue; }
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

    if (model_path.empty() && !use_rng) { std::cerr << "Error: --model or --rng is required\n"; return 1; }
    if (batch_file.empty()) {
        if (sta_file.empty())    { std::cerr << "Error: --sta is required\n";    print_usage(argv[0]); return 1; }
        if (fta_file.empty())    { std::cerr << "Error: --fta is required\n";    print_usage(argv[0]); return 1; }
        if (prompt_name.empty()) { std::cerr << "Error: --prompt is required\n"; print_usage(argv[0]); return 1; }
        if (frame_arg.empty())   { std::cerr << "Error: --frame is required\n";  print_usage(argv[0]); return 1; }
        if (ftt_file.empty())    { std::cerr << "Error: --ftt is required\n";    print_usage(argv[0]); return 1; }
    }

    llama::Manager::initialize();
    llama::ModelID model_id = 0;
    if (!use_rng) model_id = llama::Manager::add_model(model_path, ctx_size);

    struct Job { std::string sta, fta, prompt, frame, ftt, content; };
    std::vector<Job> jobs;
    if (batch_file.empty()) {
        jobs.push_back({sta_file, fta_file, prompt_name, frame_arg, ftt_file, content_arg});
    } else {
        for (auto const & j : codec::json_from_file_or_string(batch_file))
            jobs.push_back({j.at("sta").get<std::string>(), j.at("fta").get<std::string>(),
                            j.at("prompt").get<std::string>(), j.at("frame").get<std::string>(),
                            j.at("ftt").get<std::string>(), j.value("content", std::string{})});
    }

    for (Job const & job : jobs) {
        auto sta_doc = codec::from_file<data::STA>(job.sta);
        auto fta = codec::from_file<data::FTA>(job.fta);
        autocog::types::Document frame;
        codec::from_json(codec::json_from_file_or_string(job.frame), frame);
        autocog::types::Document content;
        if (!job.content.empty())
            codec::from_json(codec::json_from_file_or_string(job.content), content);

        data::FTT ftt = sta::encode_frame_to_ftt(*fta, *sta_doc, job.prompt, frame, content);
        llama::tokenize(model_id, ftt);
        if (do_score) llama::score(model_id, ftt);
        ftt.finalize();

        std::ostream * out = &std::cout;
        std::ofstream outfile;
        if (job.ftt != "/dev/stdout") {
            outfile.open(job.ftt);
            if (!outfile.is_open()) throw autocog::FileError("Failed to open: " + job.ftt, job.ftt);
            out = &outfile;
        }
        *out << codec::to_json(ftt).dump(2) << std::endl;
    }
    return 0;
}

int main(int argc, char ** argv) {
    return autocog::utilities::guard_main([&]{ return run(argc, argv); });
}
