
#include "autocog/backend/llama/convert.hxx"

#include "autocog/runtime/fta/fta.hxx"
#include "autocog/runtime/fta/ftt.hxx"
#include "autocog/backend/llama/evaluation.hxx"
#include "autocog/backend/llama/model.hxx"
#include "autocog/backend/llama/manager.hxx"

#include <nlohmann/json.hpp>

#include <optional>
#include <iostream>
#include <fstream>
#include <string>
#include "autocog/utilities/errors.hxx"
#include <algorithm>

#include "autocog/build_info.hxx"
#include "autocog/logging.hxx"

using namespace autocog::backend::llama;
using namespace autocog::runtime::fta;

void print_usage(const char* program_name) {
    std::cerr << "Usage: " << program_name << " [OPTIONS] <fta.json> ...\n"
              << "Execute FTA and output FTT JSON\n\n"
              << "Options:\n"
              << "  -c, --ctx    SIZE     Maximum context size for the model.\n"
              << "  -m, --model  PATH     Path to GGUF model file\n"
              << "  -r, --rng             Use built-in RNG model (no model file needed)\n"
              << "  -o, --output PATH     Output FTT JSON file (default: stdout)\n"
              << "  -V, --verbose [LEVEL] Log level (trace,debug,info,warn,error; default: debug)\n"
              << "  -v, --version         Show version\n"
              << "  --build-info          Show build configuration\n"
              << "  -h, --help            Show this help message\n"
              << std::endl;
}

int main(int argc, char** argv) {
  std::vector<std::string> input_files;
  std::string model_path;
  std::string output_file;
  unsigned ctx_size = 4096;
  bool use_rng = false;

  autocog::init_console_logger();

  for (int i = 1; i < argc; ++i) {
    std::string arg = argv[i];
    if (arg == "-h" || arg == "--help") { print_usage(argv[0]); return 0; }
    if (arg == "-v" || arg == "--version") { std::cout << "xfta " << autocog::version() << "\n"; return 0; }
    if (arg == "--build-info") { std::cout << autocog::build_info(); return 0; }
    if (arg == "-r" || arg == "--rng") { use_rng = true; continue; }
    if ((arg == "-m" || arg == "--model") && i + 1 < argc) { model_path = argv[++i]; continue; }
    if ((arg == "-o" || arg == "--output") && i + 1 < argc) { output_file = argv[++i]; continue; }
    if ((arg == "-c" || arg == "--ctx") && i + 1 < argc) { ctx_size = std::stoi(argv[++i]); continue; }
    if (arg == "-V" || arg == "--verbose") {
      spdlog::level::level_enum lvl = spdlog::level::debug;
      if (i + 1 < argc && argv[i + 1][0] != '-') {
        std::string ml = argv[i + 1];
        std::transform(ml.begin(), ml.end(), ml.begin(), ::tolower);
        auto p = spdlog::level::from_str(ml);
        if (p != spdlog::level::off || ml == "off") { lvl = p; ++i; }
      }
      autocog::init_console_logger(lvl);
      continue;
    }
    if (arg[0] != '-') { input_files.push_back(arg); continue; }
    std::cerr << "Error: Unknown option " << arg << "\n";
    print_usage(argv[0]);
    return 1;
  }

  if (input_files.empty()) { std::cerr << "Error: No input file specified\n"; print_usage(argv[0]); return 1; }
  if (model_path.empty() && !use_rng) { std::cerr << "Error: No model file specified (use -m or --rng)\n"; return 1; }

  try {
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

    for (auto const & input_file : input_files) {
      SPDLOG_LOGGER_DEBUG(autocog::log(), "FTA: \"{}\"", input_file);

      std::ifstream input_stream(input_file);
      if (!input_stream.is_open()) throw autocog::FileError("Failed to open: " + input_file, input_file);
      nlohmann::json fta_json;
      input_stream >> fta_json;
      input_stream.close();

      FTA fta = convert_json_to_fta(model_id, fta_json);
      EvalID eval_id = Manager::add_eval(model_id, fta);
      Manager::advance(eval_id, std::nullopt);
      FTT const & ftt = Manager::retrieve(eval_id);

      nlohmann::json ftt_json = convert_ftt_to_json(model_id, fta_json, fta, ftt);

      if (!output_file.empty()) {
        std::ofstream out(output_file);
        if (!out.is_open()) throw autocog::FileError("Failed to open: " + output_file, output_file);
        out << ftt_json.dump(2) << std::endl;
        SPDLOG_LOGGER_DEBUG(autocog::log(), "FTT: \"{}\"", output_file);
      } else {
        std::cout << ftt_json.dump(2) << std::endl;
      }

      Manager::rm_eval(eval_id);
    }
  } catch (const std::exception& e) {
    std::cerr << "Error: " << e.what() << "\n";
    return 1;
  }
  return 0;
}
