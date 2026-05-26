
#include "autocog/backend/llama/convert.hxx"

#include "autocog/runtime/fta/fta.hxx"
#include "autocog/runtime/fta/ftt.hxx"
#include "autocog/backend/llama/evaluation.hxx"
#include "autocog/backend/llama/model.hxx"
#include "autocog/backend/llama/manager.hxx"

#include <nlohmann/json.hpp>

#include <optional>
#include <functional>
#include <iostream>
#include <fstream>
#include <string>
#include <stdexcept>

using namespace autocog::backend::llama;
using namespace autocog::runtime::fta;

void print_usage(const char* program_name) {
    std::cerr << "Usage: " << program_name << " [OPTIONS] <fta.json> ...\n"
              << "Execute FTA and generate FTT\n\n"
              << "Options:\n"
              << "  -c, --ctx   SIZE     Maximum context size for the model.\n"
              << "  -m, --model PATH     Path to GGUF model file\n"
              << "  -r, --rng            Use built-in RNG model (no model file needed)\n"
              << "  -b, --best           Print best-path text to stdout (no FTT file)\n"
              << "  -v, --verbose        Verbose output\n"
              << "  -h, --help           Show this help message\n"
              << std::endl;
}

std::optional<int> parse_args(
  int argc, char** argv,
  std::vector<std::string> & input_files,
  std::string & model_path,
  unsigned & ctx_size,
  bool & verbose,
  bool & use_rng,
  bool & best_mode
) {
  for (int i = 1; i < argc; ++i) {
    std::string arg = argv[i];
    if (arg == "-h" || arg == "--help") {
      print_usage(argv[0]);
      return 0;
    } else if (arg == "-v" || arg == "--verbose") {
      verbose = true;
    } else if (arg == "-r" || arg == "--rng") {
      use_rng = true;
    } else if (arg == "-b" || arg == "--best") {
      best_mode = true;
    } else if (arg == "-m" || arg == "--model") {
      if (++i >= argc) {
        std::cerr << "Error: --model requires an argument\n";
        return 1;
      }
      model_path = argv[i];
    } else if (arg == "-c" || arg == "--ctx") {
      if (++i >= argc) {
        std::cerr << "Error: --ctx requires an argument\n";
        return 1;
      }
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

FTA read_from_json_file(ModelID model_id, std::string const & input_file) {
  std::ifstream input_stream(input_file);
  if (!input_stream.is_open()) {
    throw std::runtime_error("Failed to open input file: " + input_file);
  }
  nlohmann::json fta_json;
  input_stream >> fta_json;
  input_stream.close();
  return convert_json_to_fta(model_id, fta_json);
}

void write_to_json_file(ModelID model_id, std::string const & output_file, FTT const & ftt) {
  nlohmann::json ftt_json = convert_ftt_to_json(model_id, ftt);
  std::ofstream output_stream(output_file);
  if (!output_stream.is_open()) {
    throw std::runtime_error("Failed to open output file: " + output_file);
  }
  output_stream << ftt_json.dump(2) << std::endl;
  output_stream.close();
}

int main(int argc, char** argv) {
  std::vector<std::string> input_files;
  std::string model_path;
  unsigned ctx_size = 4096;
  bool verbose = false;
  bool use_rng = false;
  bool best_mode = false;

  std::optional<int> retval = parse_args(argc, argv, input_files, model_path, ctx_size, verbose, use_rng, best_mode);
  if (retval) return retval.value();

  try {
    Manager::initialize();
    ModelID model_id;
    if (use_rng) {
      model_id = 0;
      if (verbose) {
        std::cerr << "Using built-in RNG model (Model #0)." << std::endl;
      }
    } else {
      if (verbose) {
        std::cerr << "Loading model from " << model_path << " with " << ctx_size << " tokens of context." << std::endl;
      }
      model_id = Manager::add_model(model_path, ctx_size);
      if (verbose) {
        std::cerr << "  Model #" << model_id << std::endl;
      }
    }
    EvaluationConfig eval_cfg;
    for (auto const & input_file: input_files) {
      if (verbose) {
        std::cerr << "FTA: \"" << input_file << "\"." << std::endl;
      }
      FTA fta = read_from_json_file(model_id, input_file);
      EvalID eval_id = Manager::add_eval(model_id, fta);
      unsigned used_tokens = Manager::advance(eval_id);
      if (verbose) {
        std::cerr << "Used " << used_tokens << " tokens." << std::endl;
      }

      FTT const & ftt = Manager::retrieve(eval_id);

      if (best_mode) {
        // Walk best path and print detokenized text
        std::function<void(FTT const &)> print_best = [&](FTT const & node) {
          if (!node.tokens.empty()) {
            std::cout << Manager::get_model(model_id).detokenize(node.tokens, false, false);
          }
          for (auto const & child : node.get_children()) {
            if (!child.pruned) {
              print_best(child);
              return;
            }
          }
        };
        print_best(ftt);
      } else {
        std::string output_file = input_file + ".ftt.json";
        write_to_json_file(model_id, output_file, ftt);
        if (verbose) {
          std::cerr << "FTT: \"" << output_file << "\"." << std::endl;
        }
      }
    }

  } catch (const nlohmann::json::exception& e) {
    std::cerr << "JSON error: " << e.what() << std::endl;
    return 1;
  } catch (const std::exception& e) {
    std::cerr << "Error: " << e.what() << std::endl;
    return 1;
  } catch (...) {
    std::cerr << "Unknown error occurred" << std::endl;
    return 1;
  }
  return 0;
}
