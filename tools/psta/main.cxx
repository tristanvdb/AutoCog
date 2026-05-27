
#include "autocog/runtime/sta/load.hxx"
#include "autocog/build_info.hxx"

#include <nlohmann/json.hpp>

#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

using json = nlohmann::json;
namespace sta = autocog::runtime::sta;

static void print_usage(char const * prog) {
    std::cerr << "Usage: " << prog << " [options] <sta.json>\n\n"
              << "Score and parse FTT into field values\n\n"
              << "Options:\n"
              << "  -p, --prompt <name>    Prompt name (default: entry point)\n"
              << "  -i, --input <file>     FTT JSON (default: stdin)\n"
              << "  -o, --output <file>    Output JSON (default: stdout)\n"
              << "  --metric <name>        Scoring metric: best (default)\n"
              << "  -v, --version          Show version\n"
              << "  --build-info           Show build configuration\n"
              << "  -h, --help             Show this help\n";
}

// Walk FTT tree following best (first non-pruned) path at each branch.
// Collect nodes with "field" metadata.
static void walk_best_path(
    json const & node,
    std::vector<json const *> & path
) {
    path.push_back(&node);
    auto const & children = node["children"];
    for (auto const & child : children) {
        if (!child.value("pruned", false)) {
            walk_best_path(child, path);
            return;
        }
    }
}

// Build nested result from path nodes that have field metadata.
static json build_result(
    std::vector<json const *> const & path,
    sta::Program const & program,
    std::string const & prompt_name
) {
    auto const & prompt = program.prompts.at(prompt_name);
    json result = json::object();

    for (auto const * node : path) {
        if (!node->contains("field")) continue;

        int field_idx = (*node)["field"].get<int>();
        auto const & fld = prompt.fields[field_idx];
        std::string value = node->value("text", "");

        // Trim trailing whitespace
        while (!value.empty() && (value.back() == '\n' || value.back() == '\r'))
            value.pop_back();

        // Get indices
        std::vector<int> indices;
        if (node->contains("indices")) {
            for (auto const & idx : (*node)["indices"]) {
                indices.push_back(idx.get<int>());
            }
        }

        bool is_list = fld.is_list();

        if (fld.depth == 1) {
            // Top-level field
            if (is_list && !indices.empty()) {
                int arr_idx = indices.back();
                if (!result.contains(fld.name)) result[fld.name] = json::array();
                auto & arr = result[fld.name];
                while (static_cast<int>(arr.size()) <= arr_idx) arr.push_back(nullptr);
                arr[arr_idx] = value;
            } else {
                result[fld.name] = value;
            }
        } else {
            // Nested field: walk ancestor chain to build path
            struct Step { std::string name; int arr_idx; }; // arr_idx = -1 if not array
            std::vector<Step> chain;
            int idx = field_idx;
            int idx_cursor = static_cast<int>(indices.size());

            while (idx >= 0) {
                auto const & f = prompt.fields[idx];
                bool f_is_list = f.is_list();
                // Every field consumes one index from the end
                idx_cursor--;
                int arr_idx = (idx_cursor >= 0) ? indices[idx_cursor] : 0;
                if (f_is_list) {
                    chain.push_back({f.name, arr_idx});
                } else {
                    chain.push_back({f.name, -1});
                }
                if (f.depth == 1) break;
                // Find parent: previous field with lower depth
                int parent_idx = idx - 1;
                while (parent_idx >= 0 && prompt.fields[parent_idx].depth >= f.depth)
                    parent_idx--;
                idx = parent_idx;
            }
            std::reverse(chain.begin(), chain.end());

            // Navigate/create nested structure
            json * current = &result;
            for (size_t i = 0; i < chain.size(); ++i) {
                auto const & step = chain[i];
                bool is_last = (i == chain.size() - 1);

                if (is_last) {
                    if (step.arr_idx >= 0) {
                        if (!current->contains(step.name)) (*current)[step.name] = json::array();
                        auto & arr = (*current)[step.name];
                        while (static_cast<int>(arr.size()) <= step.arr_idx) arr.push_back(nullptr);
                        arr[step.arr_idx] = value;
                    } else {
                        (*current)[step.name] = value;
                    }
                } else {
                    if (step.arr_idx >= 0) {
                        if (!current->contains(step.name)) (*current)[step.name] = json::array();
                        auto & arr = (*current)[step.name];
                        while (static_cast<int>(arr.size()) <= step.arr_idx) arr.push_back(json::object());
                        current = &arr[step.arr_idx];
                    } else {
                        if (!current->contains(step.name)) (*current)[step.name] = json::object();
                        current = &(*current)[step.name];
                    }
                }
            }
        }
    }

    return result;
}

int main(int argc, char ** argv) {
    std::string sta_file, prompt_name, input_file, output_file;
    std::string metric = "best";

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "-h" || arg == "--help") { print_usage(argv[0]); return 0; }
        if (arg == "-v" || arg == "--version") { std::cout << "psta " << autocog::version() << "\n"; return 0; }
        if (arg == "--build-info") { std::cout << autocog::build_info(); return 0; }
        if ((arg == "-p" || arg == "--prompt") && i + 1 < argc) { prompt_name = argv[++i]; continue; }
        if ((arg == "-i" || arg == "--input") && i + 1 < argc) { input_file = argv[++i]; continue; }
        if ((arg == "-o" || arg == "--output") && i + 1 < argc) { output_file = argv[++i]; continue; }
        if (arg == "--metric" && i + 1 < argc) { metric = argv[++i]; continue; }
        if (arg[0] == '-') { std::cerr << "Unknown option: " << arg << "\n"; return 1; }
        sta_file = arg;
    }

    if (sta_file.empty()) { std::cerr << "Error: no STA file specified\n"; print_usage(argv[0]); return 1; }

    // Load STA
    json sta_json;
    {
        std::ifstream f(sta_file);
        if (!f) { std::cerr << "Error: cannot read " << sta_file << "\n"; return 1; }
        sta_json = json::parse(f);
    }

    auto program = sta::load_program(sta_json);

    // Resolve prompt name
    if (prompt_name.empty()) {
        auto const & entries = sta_json["entry_points"];
        if (entries.empty()) { std::cerr << "Error: no entry points in STA\n"; return 1; }
        prompt_name = entries.begin().value()["prompt"].get<std::string>();
    }

    if (program.prompts.find(prompt_name) == program.prompts.end()) {
        std::cerr << "Error: prompt '" << prompt_name << "' not found in STA\n";
        return 1;
    }

    // Read FTT JSON
    json ftt_json;
    if (!input_file.empty()) {
        std::ifstream f(input_file);
        if (!f) { std::cerr << "Error: cannot read " << input_file << "\n"; return 1; }
        ftt_json = json::parse(f);
    } else {
        ftt_json = json::parse(std::cin);
    }

    // Walk best path
    std::vector<json const *> path;
    walk_best_path(ftt_json, path);

    // Build result
    json result = build_result(path, program, prompt_name);

    // Output
    std::ostream * out = &std::cout;
    std::ofstream outfile;
    if (!output_file.empty()) {
        outfile.open(output_file);
        out = &outfile;
    }
    *out << result.dump(2) << std::endl;

    return 0;
}
