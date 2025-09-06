
#include "convert.hxx"

#include "autocog/compiler/stl/diagnostic.hxx"
#include "autocog/compiler/stl/parser.hxx"
#include "autocog/compiler/stl/ast.hxx"
#include "autocog/compiler/stl/instantiate.hxx"

#include <optional>
#include <iostream>
#include <fstream>
#include <cstring>
#include <queue>

using namespace autocog::compiler::stl;

// TODO long form args like for xfta
void print_usage(const char* program) {
    std::cerr << "Usage: " << program << " [options] <input.stl>\n";
    std::cerr << "Options:\n";
    std::cerr << "  -o <output.json>  Write JSON IR to file (default: stdout)\n";
    std::cerr << "  -V                Verbose\n";
    std::cerr << "  -I <path>         Add to import search paths\n";
    std::cerr << "  -h                Show this help\n";
}

bool report_errors(
  std::list<Diagnostic> & diagnostics,
  std::unordered_map<std::string, int> const & fileids,
  unsigned & errors,
  unsigned & warnings,
  unsigned & notes
) {
    for (auto const & diag : diagnostics) {
        std::cerr << diag.format(fileids) << std::endl;
        switch (diag.level) {
          case DiagnosticLevel::Error:   errors++;   break;
          case DiagnosticLevel::Warning: warnings++; break;
          case DiagnosticLevel::Note:    notes++;    break;
        }
    }

    if (errors > 0) {
        std::cerr << "Failed with " << errors << " error(s), " << warnings << " warning(s), and " << notes << " note(s).\n";
    } else if (warnings > 0) {
        std::cerr << "Passed with " << warnings << " warning(s) and " << notes << " note(s).\n";
    } else if (notes > 0) {
        std::cerr << "Passed with " << notes << " note(s).\n";
    }
    diagnostics.clear();

    return errors > 0;
}

std::optional<int> parse_args(
    int argc, char** argv,
    std::list<std::string> & input_files,
    std::string & output_file,
    std::list<std::string> & search_paths,
    bool & verbose
) {
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "-h") == 0) {
            print_usage(argv[0]);
            return 0;
        } else if (std::strcmp(argv[i], "-o") == 0) {
            if (i + 1 < argc) {
                output_file = argv[++i];
            } else {
                std::cerr << "Error: -o requires an argument" << std::endl;
                return 1;
            }
        } else if (std::strcmp(argv[i], "-I") == 0) {
            if (i + 1 < argc) {
                search_paths.push_back(argv[++i]);
            } else {
                std::cerr << "Error: -I requires an argument" << std::endl;
                return 1;
            }
        } else if (std::strcmp(argv[i], "-V") == 0) {
            verbose = true;
        } else if (argv[i][0] == '-') {
            std::cerr << "Error: Unknown option " << argv[i] << std::endl;
            print_usage(argv[0]);
            return 1;
        } else {
            input_files.push_back(argv[i]);
        }
    }

    if (input_files.empty()) {
        std::cerr << "Error: No input file specified" << std::endl;
        print_usage(argv[0]);
        return 1;
    }

    return std::nullopt;
}

int main(int argc, char** argv) {

    // Parge CLI arguments

    std::string output_file;
    std::list<std::string> file_paths;
    std::list<std::string> search_paths;
    bool verbose = false;
    
    std::optional<int> retval = parse_args(argc, argv, file_paths, output_file, search_paths, verbose);
    if (retval) return retval.value();
    
    // Create parser

    unsigned errors = 0;
    unsigned warnings = 0;
    unsigned notes = 0;
    std::list<Diagnostic> diagnostics;
    std::unordered_map<std::string, int> fileids;
    Parser parser(diagnostics, fileids, search_paths, file_paths);

    // Parse all files

    parser.parse();
    if (report_errors(diagnostics, fileids, errors, warnings, notes)) return 1;

    // Instantiate all exported prompts associated to input files

    Instantiator instantiator(diagnostics);
    for (auto const & [filepath,program]: parser.get()) {
        instantiator.defines(program);
        instantiator.declarations(program);
        instantiator.entries(program);
    }
    instantiator.collect();
    instantiator.instantiate();
    if (report_errors(diagnostics, fileids, errors, warnings, notes)) return 1;

    return 0;
}
