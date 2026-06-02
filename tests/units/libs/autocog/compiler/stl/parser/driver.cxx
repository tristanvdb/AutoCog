
#include "autocog/compiler/stl/parser.hxx"
#include "autocog/utilities/exception.hxx"
#include <nlohmann/json.hpp>

#include <sstream>
#include <iostream>
#include <fstream>
#include <iomanip>
#include <algorithm>
#include <cctype>

using json = nlohmann::json;

namespace autocog::compiler::stl::test {

struct ParseDriver {
    int tests_run = 0;
    int tests_passed = 0;
    int tests_failed = 0;

    std::optional<int> run_test_file(const std::string & json_file) {
        std::ifstream file(json_file);
        if (!file.is_open()) {
            std::ostringstream oss;
            oss << "Error: Cannot open test file: " << json_file;
            throw std::runtime_error(oss.str());
        }
        
        nlohmann::json test_data;
        file >> test_data;
        
        std::cout << "Running STL Parser Tests from: " << json_file << "\n";
        std::cout << std::string(60, '=') << "\n";

        for (auto& test : test_data) {
            tests_run++;
            
            std::string description = test.value("description", "No description");
            std::string tag = test["tag"];
            std::string code = test["code"];
            bool should_pass = test.value("should_pass", true);
            // Optional: when failing is expected, assert the diagnostic message
            // contains this substring (a proper diagnostic, not just any failure).
            std::string error_contains = test.value("error_contains", "");

            bool parse_succeeded = false;
            std::string error_message;

            try {
                parse_succeeded = Parser::parse_fragment(tag, code);
                if (!parse_succeeded) error_message = "Parser::parse_fragment returned false. Likely because of remaining tokens in the stream.";
            } catch (ParseError const & e) {
                parse_succeeded = false;
                error_message = e.message;
            }

            bool test_passed = (parse_succeeded == should_pass);
            std::string failure_detail;

            // An internal error is never an acceptable diagnostic.
            auto lower = error_message;
            std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
            if (lower.find("internal error") != std::string::npos) {
                test_passed = false;
                failure_detail = "diagnostic is an 'internal error' (never acceptable)";
            }
            // When a specific diagnostic is expected, require it.
            else if (!error_contains.empty()) {
                if (parse_succeeded) {
                    test_passed = false;
                    failure_detail = "expected failure with diagnostic, but parse succeeded";
                } else if (error_message.find(error_contains) == std::string::npos) {
                    test_passed = false;
                    failure_detail = "diagnostic did not contain expected substring: " + error_contains;
                }
            }

            if (test_passed) {
                tests_passed++;
                std::cout << "✓ Test #" << tests_run << ": " << description << "\n";
                std::cout << "  Tag: " << tag << "\n";
                std::cout << "  Code: " << code << "\n";
                if (!should_pass) {
                    std::cout << "  (Correctly failed as expected)\n";
                }
            } else {
                tests_failed++;
                std::cout << "❌ Test #" << tests_run << ": " << description << "\n";
                std::cout << "  Tag: " << tag << "\n";
                std::cout << "  Code: " << code << "\n";
                if (!failure_detail.empty()) {
                    std::cout << "  " << failure_detail << "\n";
                    std::cout << "  Error: " << error_message << "\n";
                } else if (should_pass) {
                    std::cout << "  Expected: PASS, Got: FAIL\n";
                    std::cout << "  Error: " << error_message << "\n";
                } else {
                    std::cout << "  Expected: FAIL, Got: PASS\n";
                }
            }
            std::cout << "\n";
        }
        
        // Print summary
        std::cout << std::string(60, '=') << "\n";
        std::cout << "Test Summary:\n";
        std::cout << "  Total: " << tests_run << "\n";
        std::cout << "  Passed: " << tests_passed << "\n";
        std::cout << "  Failed: " << tests_failed << "\n";
        std::cout << "  Success Rate: " << std::fixed << std::setprecision(1) 
                  << (tests_run > 0 ? (100.0 * tests_passed / tests_run) : 0) << "%\n";

        return tests_failed;
    }
};

}

using Driver = autocog::compiler::stl::test::ParseDriver;
int main(int argc, char* argv[]) {
    if (argc != 2) {
        std::cerr << "Usage: " << argv[0] << " <test_file.json>\n";
        return 1;
    }
    Driver driver;
    try {
        auto rc = driver.run_test_file(argv[1]);
        return rc ? rc.value() : 0;
    } catch (std::exception const & e) {
        std::cerr << "Error: " << e.what() << "\n";
        return 1;
    }
}
