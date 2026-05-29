
#include "autocog/compiler/stl/driver.hxx"
#include "autocog/logging.hxx"
#include "autocog/utilities/errors.hxx"

#include <algorithm>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>
#include <list>
#include <optional>
#include <variant>
#include <unordered_map>
#include <cstring>
#include <sstream>
#include <regex>

#include "autocog/build_info.hxx"

using namespace autocog::compiler::stl;

static void print_usage(const char* program) {
  std::cerr << "Usage: " << program << " [options] [files...]\n";
  std::cerr << "\n";
  std::cerr << "Compile STL files to JSON intermediate representation\n";
  std::cerr << "\n";
  std::cerr << "Arguments:\n";
  std::cerr << "  files          Input STL files (can also use -i)\n";
  std::cerr << "\n";
  std::cerr << "Options:\n";
  std::cerr << "  -i, --input <file>     Input STL file\n";
  std::cerr << "  -o, --output <file>    Write JSON IR to file (default: stdout)\n";
  std::cerr << "  -I, --include <path>   Add to import search paths\n";
  std::cerr << "  -D, --define <var[:<type>]=<value>>\n";
  std::cerr << "               Define a variable with optional type\n";
  std::cerr << "               Examples:\n";
  std::cerr << "               -Dflag       (bool, true)\n";
  std::cerr << "               -Dcount=5    (int)\n";
  std::cerr << "               -Dpi=3.14    (float)\n";
  std::cerr << "               -Dname=\"text\"  (string)\n";
  std::cerr << "               -Dval:int=42   (explicit type)\n";
  std::cerr << "  -e, --emit <target>    Emit target (default: sta)\n";
  std::cerr << "               Targets: ast, symbols, globals, graph,\n";
  std::cerr << "                        ir, sta (default)\n";
  std::cerr << "  -V, --verbose [LEVEL]  Set log level (trace,debug,info,warn,error,critical)\n"
              << "                         Default: debug if bare, warn if absent\n";
  std::cerr << "  -h, --help         Show this help message\n";
  std::cerr << "  -v, --version      Show version information\n";
  std::cerr << "  --build-info       Show build configuration\n";
}

static void print_version(const char* program) {
  std::cout << program << " " << autocog::version() << "\n";
}

static void print_build_info() {
  std::cout << autocog::build_info();
}

// Helper to check if a string looks like a number
static bool is_integer(const std::string& s) {
  if (s.empty()) return false;
  size_t start = (s[0] == '-' || s[0] == '+') ? 1 : 0;
  if (start >= s.length()) return false;
  for (size_t i = start; i < s.length(); ++i) {
    if (!std::isdigit(s[i])) return false;
  }
  return true;
}

static bool is_float(const std::string& s) {
  if (s.empty()) return false;
  bool has_dot = false;
  bool has_e = false;
  size_t start = (s[0] == '-' || s[0] == '+') ? 1 : 0;
  if (start >= s.length()) return false;

  for (size_t i = start; i < s.length(); ++i) {
    if (s[i] == '.') {
      if (has_dot || has_e) return false;
      has_dot = true;
    } else if (s[i] == 'e' || s[i] == 'E') {
      if (has_e || i == start || i == s.length() - 1) return false;
      has_e = true;
      // Allow + or - after e
      if (i + 1 < s.length() && (s[i + 1] == '+' || s[i + 1] == '-')) {
        i++;
      }
    } else if (!std::isdigit(s[i])) {
      return false;
    }
  }
  return has_dot || has_e;
}

static bool is_bool(const std::string& s) {
  return s == "true" || s == "false";
}

// Remove quotes from string if present
static std::string unquote(const std::string& s) {
  if (s.length() >= 2) {
    if ((s[0] == '"' && s[s.length() - 1] == '"') ||
      (s[0] == '\'' && s[s.length() - 1] == '\'')) {
      return s.substr(1, s.length() - 2);
    }
  }
  return s;
}

// Parse an emit target argument
static bool parse_emit(const std::string& arg, Driver& driver) {
  if (arg == "ast") {
    driver.stage = CompilationStage::Parse;
  } else if (arg == "symbols") {
    driver.stage = CompilationStage::Symbols;
  } else if (arg == "globals") {
    driver.stage = CompilationStage::Globals;
  } else if (arg == "graph") {
    driver.stage = CompilationStage::Instantiate;
  } else if (arg == "ir") {
    driver.stage = CompilationStage::Assemble;
  } else if (arg == "sta") {
    driver.stage = CompilationStage::Generate;
  } else {
    std::cerr << "Error: Invalid emit target: " << arg << std::endl;
    std::cerr << "Valid targets: ast, symbols, globals, graph, ir, sta" << std::endl;
    return false;
  }
  return true;
}

// Parse a define argument
static bool parse_define(const std::string& arg, Driver& driver) {
  // Find the position of '=' and ':'
  size_t eq_pos = arg.find('=');
  size_t colon_pos = arg.find(':');

  // Case 1: Just variable name (implicit bool=true)
  if (eq_pos == std::string::npos) {
    driver.defines[arg] = true;
    return true;
  }

  std::string var_name;
  std::string type_str;
  std::string value_str = arg.substr(eq_pos + 1);

  // Case 2: var:type=value
  if (colon_pos != std::string::npos && colon_pos < eq_pos) {
    var_name = arg.substr(0, colon_pos);
    type_str = arg.substr(colon_pos + 1, eq_pos - colon_pos - 1);
  } 
  // Case 3: var=value (infer type)
  else {
    var_name = arg.substr(0, eq_pos);
  }

  if (var_name.empty()) {
    std::cerr << "Error: Invalid define format: " << arg << std::endl;
    return false;
  }

  // Parse value based on type
  if (!type_str.empty()) {
    // Explicit type
    if (type_str == "bool") {
      if (value_str == "true") {
        driver.defines[var_name] = true;
      } else if (value_str == "false") {
        driver.defines[var_name] = false;
      } else {
        std::cerr << "Error: Invalid bool value: " << value_str << std::endl;
        return false;
      }
    } else if (type_str == "int") {
      try {
        driver.defines[var_name] = std::stoi(value_str);
      } catch (...) {
        std::cerr << "Error: Invalid int value: " << value_str << std::endl;
        return false;
      }
    } else if (type_str == "float") {
      try {
        driver.defines[var_name] = std::stof(value_str);
      } catch (...) {
        std::cerr << "Error: Invalid float value: " << value_str << std::endl;
        return false;
      }
    } else if (type_str == "string") {
      driver.defines[var_name] = unquote(value_str);
    } else {
      std::cerr << "Error: Unknown type: " << type_str << std::endl;
      return false;
    }
  } else {
    // Infer type from value
    if (is_bool(value_str)) {
      driver.defines[var_name] = (value_str == "true");
    } else if (is_integer(value_str)) {
      driver.defines[var_name] = std::stoi(value_str);
    } else if (is_float(value_str)) {
      driver.defines[var_name] = std::stof(value_str);
    } else {
      // Default to string (remove quotes if present)
      driver.defines[var_name] = unquote(value_str);
    }
  }

  return true;
}

std::optional<int> parse_args(int argc, char** argv, Driver & driver) {
  for (int i = 1; i < argc; ++i) {
    std::string arg = argv[i];

    // Help
    if (arg == "-h" || arg == "--help") {
      print_usage(argv[0]);
      return 0;
    }

    // Version
    if (arg == "-v" || arg == "--version") {
      print_version(argv[0]);
      return 0;
    }

    // Build info
    if (arg == "--build-info") {
      print_build_info();
      return 0;
    }

    // Verbose
    if (arg == "-V" || arg == "--verbose") {
      spdlog::level::level_enum lvl = spdlog::level::debug;  // bare --verbose -> DEBUG
      if (i + 1 < argc && autocog::looks_like_level_token(argv[i + 1])) {
        if (autocog::parse_level(argv[i + 1], lvl)) {
          ++i;  // consume the level arg
        } else {
          std::cerr << "Error: unknown verbosity level '" << argv[i + 1]
                    << "' (expected: trace, debug, info, warn, error, critical, off)\n";
          return 1;
        }
      }
      autocog::init_console_logger(lvl);
      continue;
    }

    // Input file
    if (arg == "-i" || arg == "--input") {
      if (i + 1 < argc) {
        driver.inputs.push_back(argv[++i]);
      } else {
        std::cerr << "Error: " << arg << " requires an argument" << std::endl;
        return 1;
      }
      continue;
    }

    // Output file
    if (arg == "-o" || arg == "--output") {
      if (i + 1 < argc) {
        driver.output = argv[++i];
      } else {
        std::cerr << "Error: " << arg << " requires an argument" << std::endl;
        return 1;
      }
      continue;
    }

    // Include path - handle both -I<path> and -I <path>
    if (arg == "-I" || arg == "--include") {
      if (i + 1 < argc) {
        driver.includes.push_back(argv[++i]);
      } else {
        std::cerr << "Error: " << arg << " requires an argument" << std::endl;
        return 1;
      }
      continue;
    }
    if (arg.length() > 2 && arg.substr(0, 2) == "-I") {
      driver.includes.push_back(arg.substr(2));
      continue;
    }

    // Define - handle both -D<define> and -D <define>
    if (arg == "-D" || arg == "--define") {
      if (i + 1 < argc) {
        if (!parse_define(argv[++i], driver)) {
          return 1;
        }
      } else {
        std::cerr << "Error: " << arg << " requires an argument" << std::endl;
        return 1;
      }
      continue;
    }
    if (arg.length() > 2 && arg.substr(0, 2) == "-D") {
      if (!parse_define(arg.substr(2), driver)) {
        return 1;
      }
      continue;
    }

    // Emit target
    if (arg == "-e" || arg == "--emit") {
      if (i + 1 < argc) {
        if (!parse_emit(argv[++i], driver)) {
          return 1;
        }
      } else {
        std::cerr << "Error: " << arg << " requires an argument" << std::endl;
        return 1;
      }
      continue;
    }

    // Unknown option
    if (arg[0] == '-') {
      std::cerr << "Error: Unknown option " << arg << std::endl;
      print_usage(argv[0]);
      return 1;
    }

    // Positional argument (input file)
    driver.inputs.push_back(arg);
  }

  // Validate we have at least one input
  if (driver.inputs.empty()) {
    std::cerr << "Error: No input files specified" << std::endl;
    print_usage(argv[0]);
    return 1;
  }

  // A missing top-level input is a file error, not a compile diagnostic (there
  // is no source to point into). Caught and reported by guard_main in main().
  for (auto const & input : driver.inputs) {
    if (!std::filesystem::exists(input)) {
      throw autocog::FileError("Cannot find file: " + input, input);
    }
  }

  // Log parsed arguments
  SPDLOG_LOGGER_DEBUG(autocog::log(), "Inputs: {}, Includes: {}",
                      driver.inputs.size(), driver.includes.size());

  return std::nullopt;
}

