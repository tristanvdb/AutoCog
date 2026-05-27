
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
#include <stdexcept>

#include "autocog/build_info.hxx"

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
              << "  -v, --verbose         Verbose output\n"
              << "  --version             Show version\n"
              << "  --build-info          Show build configuration\n"
              << "  -h, --help            Show this help message\n"
              << std::endl;
}

std::optional<int> parse_args(
  int argc, char** argv,
  std::vector<std::string> & input_files,
  std::string & model_path,
  std::string & output_file,
  unsigned & ctx_size,
  bool & verbose,
  bool & use_rng
) {
  for (int i = 1; i < argc; ++i) {
    std::string arg = argv[i];
    if (arg == "-h" || arg == "--help") {
      print_usage(argv[0]);
      return 0;
    } else if (arg == "--version") {
      std::cout << "xfta " << autocog::version() << "\n";
      return 0;
    } else if (arg == "--build-info") {
      std::cout << autocog::build_info();
      return 0;
    } else if (arg == "-v" || arg == "--verbose") {
      verbose = true;
    } else if (arg == "-r" || arg == "--rng") {
      use_rng = true;
    } else if (arg == "-m" || arg == "--model") {
      if (++i >= argc) { std::cerr << "Error: --model requires an argument\n"; return 1; }
      model_path = argv[i];
    } else if (arg == "-o" || arg == "--output") {
      if (++i >= argc) { std::cerr << "Error: --output requires an argument\n"; return 1; }
      output_file = argv[i];
    } else if (arg == "-c" || arg == "--ctx") {
      if (++i >= argc) { std::cerr << "Error: --ctx requires an argument\n"; return 1; }
      ctx_size = std::stoi(argv[i]);
    } else if (arg[0] != '-') {
      input_files.push_back(arg);
    } else {
      std::cerr << "Error: Unknown option " << arg << "\n";
      print_usage(argv[0]);
      return 1;
    }
  }

  if (input_files.empty()) {
    std::cerr << "Error: No input file specified\n";
    print_usage(argv[0]);
    return 1;
  }

  if (model_path.empty() && !use_rng) {
    std::cerr << "Error: No model file specified (use -m or --rng)\n";
    print_usage(argv[0]);
    return 1;
  }

  return std::nullopt;
}

int main(int argc, char** argv) {
  std::vector<std::string> input_files;
  std::string model_path;
  std::string output_file;
  unsigned ctx_size = 4096;
  bool verbose = false;
  bool use_rng = false;

  std::optional<int> retval = parse_args(argc, argv, input_files, model_path, output_file, ctx_size, verbose, use_rng);
  if (retval) return retval.value();

  try {
    Manager::initialize();

    ModelID model_id;
    if (use_rng) {
      model_id = 0;
      if (verbose) std::cerr << "Using built-in RNG model (Model #0)." << std::endl;
    } else {
      if (verbose) std::cerr << "Loading model from " << model_path << " with " << ctx_size << " tokens of context." << std::endl;
      model_id = Manager::add_model(model_path, ctx_size);
      if (verbose) std::cerr << "  Model #" << model_id << std::endl;
    }

    for (auto const & input_file : input_files) {
      if (verbose) std::cerr << "FTA: \"" << input_file << "\"." << std::endl;

      // Read FTA
      std::ifstream input_stream(input_file);
      if (!input_stream.is_open()) throw std::runtime_error("Failed to open: " + input_file);
      nlohmann::json fta_json;
      input_stream >> fta_json;
      input_stream.close();

      FTA fta = convert_json_to_fta(model_id, fta_json);

      // Evaluate
      EvalID eval_id = Manager::add_eval(model_id, fta);
      Manager::advance(eval_id, std::nullopt);
      FTT const & ftt = Manager::retrieve(eval_id);

      // Output FTT JSON
      nlohmann::json ftt_json = convert_ftt_to_json(model_id, fta_json, fta, ftt);

      if (!output_file.empty()) {
        std::ofstream out(output_file);
        if (!out.is_open()) throw std::runtime_error("Failed to open: " + output_file);
        out << ftt_json.dump(2) << std::endl;
        if (verbose) std::cerr << "  FTT: \"" << output_file << "\"." << std::endl;
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
