
#include "autocog/backend/llama/evaluation.hxx"
#include "autocog/backend/llama/model.hxx"
#include "autocog/backend/llama/manager.hxx"
#include "autocog/backend/llama/prepared.hxx"

#include "autocog/codec/json.hxx"
#include "autocog/data/fta.hxx"
#include "autocog/data/ftt.hxx"

#include <nlohmann/json.hpp>

#include <optional>
#include <iostream>
#include <fstream>
#include <string>
#include "autocog/utilities/errors.hxx"
#include "autocog/utilities/exception.hxx"
#include <algorithm>

#include "autocog/build_info.hxx"
#include "autocog/logging.hxx"

using namespace autocog::backend::llama;
namespace data  = autocog::data;
namespace codec = autocog::codec;

void print_usage(const char* program_name) {
    std::cerr << "Usage: " << program_name << " --fta <file> (--model <file> | --rng) --ftt <file>\n"
              << "            [--seed N] [--ctx N]\n\n"
              << "Evaluate an FTA against a model and write the resulting FTT.\n\n"
              << "Options:\n"
              << "  --fta <file>          Input FTA JSON (required)\n"
              << "  --model <file>        Path to GGUF model file\n"
              << "  --rng                 Use built-in RNG model (no model file needed)\n"
              << "  --ftt <file>          Output FTT JSON (required; /dev/stdout for stdout)\n"
              << "  --seed N              RNG seed (default: 42)\n"
              << "  --ctx N               Maximum context size for the model\n"
              << "  --verbose [LEVEL]     Log level (trace,debug,info,warn,error; default: debug)\n"
              << "  --version             Show version\n"
              << "  --build-info          Show build configuration\n"
              << "  --help                Show this help message\n"
              << std::endl;
}

static int run(int argc, char** argv) {
  std::string fta_file, ftt_file, model_path;
  unsigned ctx_size = 4096;
  unsigned seed = 42;
  bool use_rng = false;

  autocog::init_console_logger();

  for (int i = 1; i < argc; ++i) {
    std::string arg = argv[i];
    if (arg == "--help") { print_usage(argv[0]); return 0; }
    if (arg == "--version") { std::cout << "xfta " << autocog::version() << "\n"; return 0; }
    if (arg == "--build-info") { std::cout << autocog::build_info(); return 0; }
    if (arg == "--rng") { use_rng = true; continue; }
    if (arg == "--fta"   && i + 1 < argc) { fta_file = argv[++i]; continue; }
    if (arg == "--ftt"   && i + 1 < argc) { ftt_file = argv[++i]; continue; }
    if (arg == "--model" && i + 1 < argc) { model_path = argv[++i]; continue; }
    if (arg == "--seed"  && i + 1 < argc) { seed = std::stoul(argv[++i]); continue; }
    if (arg == "--ctx"   && i + 1 < argc) { ctx_size = std::stoi(argv[++i]); continue; }
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
    std::cerr << "Error: Unknown option " << arg << "\n";
    print_usage(argv[0]);
    return 1;
  }

  if (fta_file.empty()) { std::cerr << "Error: --fta is required\n"; print_usage(argv[0]); return 1; }
  if (ftt_file.empty()) { std::cerr << "Error: --ftt is required\n"; print_usage(argv[0]); return 1; }
  if (model_path.empty() && !use_rng) { std::cerr << "Error: --model or --rng is required\n"; return 1; }

  Manager::initialize();

  ModelID model_id;
  if (use_rng) {
    model_id = 0;
    SPDLOG_LOGGER_DEBUG(autocog::log(), "Using built-in RNG model (Model #0)");
  } else {
    SPDLOG_LOGGER_DEBUG(autocog::log(), "Loading model from {} with {} tokens of context", model_path, ctx_size);
    model_id = Manager::add_model(model_path, ctx_size);
    SPDLOG_LOGGER_DEBUG(autocog::log(), "Model #{}", model_id);
  }

  Manager::get_model(model_id).set_seed(seed);
  SPDLOG_LOGGER_DEBUG(autocog::log(), "RNG seed: {}", seed);

  SPDLOG_LOGGER_DEBUG(autocog::log(), "FTA: \"{}\"", fta_file);
  auto fta = codec::from_file<data::FTA>(fta_file);
  EvalID eval_id = Manager::add_eval(model_id, *fta);
  Manager::advance(eval_id, std::nullopt);

  // The backend grows the tree with tokens during evaluation; fill each node's
  // text from its tokens in one post-generation pass (needs the model).
  data::FTT ftt = Manager::retrieve(eval_id);
  detokenize(model_id, ftt);

  std::ostream * out = &std::cout;
  std::ofstream outfile;
  if (ftt_file != "/dev/stdout") {
    outfile.open(ftt_file);
    if (!outfile.is_open()) throw autocog::FileError("Failed to open: " + ftt_file, ftt_file);
    out = &outfile;
  }
  *out << codec::to_json(ftt).dump(2) << std::endl;
  SPDLOG_LOGGER_DEBUG(autocog::log(), "FTT: \"{}\"", ftt_file);

  Manager::rm_eval(eval_id);
  return 0;
}

int main(int argc, char** argv) {
  return autocog::utilities::guard_main([&]{ return run(argc, argv); });
}
