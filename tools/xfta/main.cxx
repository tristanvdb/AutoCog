
#include "convert.hxx"

#include "autocog/llama/xfta/fta.hxx"
#include "autocog/llama/xfta/ftt.hxx"
#include "autocog/llama/xfta/evaluation.hxx"
#include "autocog/llama/xfta/model.hxx"

#include <nlohmann/json.hpp>

#include <iostream>
#include <fstream>
#include <string>
#include <stdexcept>

using namespace autocog::llama::xfta;

void print_usage(const char* program_name) {
    std::cerr << "Usage: " << program_name << " [OPTIONS] <fta.json>\n"
              << "Execute FTA and generate FTT\n\n"
              << "Options:\n"
              << "  -o, --output FILE    Output FTT to FILE (default: stdout)\n"
              << "  -m, --model PATH     Path to GGUF model file\n"
              << "  -v, --verbose        Verbose output\n"
              << "  -h, --help           Show this help message\n";
}

int main(int argc, char** argv) {
    // Parse command line arguments
    std::string input_file;
    std::string output_file;
    std::string model_path;
    bool verbose = false;
    
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        
        if (arg == "-h" || arg == "--help") {
            print_usage(argv[0]);
            return 0;
        } else if (arg == "-v" || arg == "--verbose") {
            verbose = true;
        } else if (arg == "-o" || arg == "--output") {
            if (++i >= argc) {
                std::cerr << "Error: --output requires an argument\n";
                return 1;
            }
            output_file = argv[i];
        } else if (arg == "-m" || arg == "--model") {
            if (++i >= argc) {
                std::cerr << "Error: --model requires an argument\n";
                return 1;
            }
            model_path = argv[i];
        } else if (arg[0] != '-') {
            if (!input_file.empty()) {
                std::cerr << "Error: Multiple input files specified\n";
                return 1;
            }
            input_file = arg;
        } else {
            std::cerr << "Error: Unknown option " << arg << "\n";
            print_usage(argv[0]);
            return 1;
        }
    }
    
    if (input_file.empty()) {
        std::cerr << "Error: No input file specified\n";
        print_usage(argv[0]);
        return 1;
    }
    
    if (model_path.empty()) {
        std::cerr << "Error: No model file specified\n";
        print_usage(argv[0]);
        return 1;
    }
    
    try {
        // Read FTA from JSON file
        if (verbose) {
            std::cerr << "Reading FTA from " << input_file << "...\n";
        }
        
        std::ifstream input_stream(input_file);
        if (!input_stream.is_open()) {
            throw std::runtime_error("Failed to open input file: " + input_file);
        }
        
        nlohmann::json fta_json;
        input_stream >> fta_json;
        input_stream.close();
        
        // TODO: Convert JSON to FTA object
        // This will depend on the actual FTA class structure
        // FTA fta = FTA::from_json(fta_json);
        
        if (verbose) {
            std::cerr << "Loaded FTA with " << fta_json["states"].size() 
                      << " states\n";
        }
        
        // Initialize model
        if (verbose) {
            std::cerr << "Loading model from " << model_path << "...\n";
        }
        
        // TODO: Initialize the model
        // Model model(model_path);
        
        // Execute FTA to generate FTT
        if (verbose) {
            std::cerr << "Executing FTA to generate FTT...\n";
        }
        
        // TODO: Execute the FTA
        // Evaluator evaluator(model);
        // FTT ftt = evaluator.evaluate(fta);
        
        // For now, create a dummy FTT JSON
        nlohmann::json ftt_json = {
            {"status", "success"},
            {"execution_time_ms", 0},
            {"tokens_generated", 0},
            {"trace", nlohmann::json::array()},
            // TODO: Add actual FTT structure
        };
        
        // Write FTT to output
        if (output_file.empty()) {
            // Write to stdout
            std::cout << ftt_json.dump(2) << std::endl;
        } else {
            // Write to file
            if (verbose) {
                std::cerr << "Writing FTT to " << output_file << "...\n";
            }
            
            std::ofstream output_stream(output_file);
            if (!output_stream.is_open()) {
                throw std::runtime_error("Failed to open output file: " + output_file);
            }
            
            output_stream << ftt_json.dump(2) << std::endl;
            output_stream.close();
        }
        
        if (verbose) {
            std::cerr << "Successfully generated FTT\n";
        }
        
        return 0;
        
    } catch (const nlohmann::json::exception & e) {
        std::cerr << "JSON error: " << e.what() << std::endl;
        return 1;
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    } catch (...) {
        std::cerr << "Unknown error occurred" << std::endl;
        return 1;
    }
}
