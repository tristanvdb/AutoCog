#include "autocog/utilities/exception.hxx"

#include <iostream>
#include <exception>
#include <execinfo.h>
#include <cxxabi.h>
#include <dlfcn.h>
#include <sstream>
#include <vector>
#include <memory>
#include <regex>
#include <array>
#include <cstring>
#include <iomanip>
#include <stdexcept>
#include <algorithm>

namespace autocog::utilities {

InternalError::InternalError(
  std::string msg
) :
  message(std::move(msg))
{}
  
const char * InternalError::what() const noexcept {
  return message.c_str();
}

thread_local Backtrace g_last_throw_backtrace;

namespace {

// Helper to demangle a C++ symbol
std::string demangle(const char* name) {
    if (!name) return "???";
    
    int status = -1;
    std::unique_ptr<char, void(*)(void*)> demangled(
        abi::__cxa_demangle(name, nullptr, nullptr, &status),
        std::free
    );
    
    return (status == 0 && demangled) ? std::string(demangled.get()) : std::string(name);
}

// Helper to run addr2line and get file:line info
struct Addr2LineResult {
    std::string function;
    std::string file;
    int line;
    bool success;
    
    Addr2LineResult() : line(-1), success(false) {}
};

Addr2LineResult addr2line(const std::string& executable, void* addr) {
    Addr2LineResult result;
    
    char cmd[512];
    snprintf(cmd, sizeof(cmd), "addr2line -e %s -f -C -i %p 2>/dev/null", 
             executable.c_str(), addr);
    
    std::unique_ptr<FILE, decltype(&pclose)> pipe(popen(cmd, "r"), pclose);
    if (!pipe) {
        return result;
    }
    
    std::array<char, 256> buffer;
    std::vector<std::string> lines;
    
    while (fgets(buffer.data(), buffer.size(), pipe.get()) != nullptr) {
        std::string line(buffer.data());
        // Remove trailing newline
        while (!line.empty() && (line.back() == '\n' || line.back() == '\r')) {
            line.pop_back();
        }
        lines.push_back(line);
    }
    
    // addr2line output format:
    // function_name
    // file:line
    if (lines.size() >= 2 && lines[0] != "??" && lines[1] != "??:0") {
        result.function = lines[0];
        
        // Parse file:line
        size_t colon_pos = lines[1].rfind(':');
        if (colon_pos != std::string::npos) {
            result.file = lines[1].substr(0, colon_pos);
            try {
                result.line = std::stoi(lines[1].substr(colon_pos + 1));
                result.success = true;
            } catch (...) {
                // Invalid line number
            }
        }
    }
    
    return result;
}

// Parse backtrace symbol and extract components
struct SymbolInfo {
    std::string module;
    std::string symbol;
    std::string offset;
    std::string address;
    
    explicit SymbolInfo(const std::string& raw) {
        // Format: module(symbol+offset) [address]
        // or: module() [address]
        // or: module [address]
        
        std::regex re(R"(^(.*?)\((.*?)\)\s+\[(.*?)\]$)");
        std::smatch match;
        
        if (std::regex_match(raw, match, re)) {
            module = match[1];
            address = match[3];
            
            // Parse symbol+offset
            std::string sym_off = match[2];
            size_t plus_pos = sym_off.find('+');
            if (plus_pos != std::string::npos) {
                symbol = sym_off.substr(0, plus_pos);
                offset = sym_off.substr(plus_pos);
            } else {
                symbol = sym_off;
            }
        } else {
            // Fallback: try to at least get module
            size_t bracket_pos = raw.find('[');
            if (bracket_pos != std::string::npos) {
                module = raw.substr(0, bracket_pos);
                // Trim whitespace
                while (!module.empty() && std::isspace(module.back())) {
                    module.pop_back();
                }
            }
        }
    }
};

// Extract basename from path
std::string basename(const std::string& path) {
    size_t pos = path.rfind('/');
    return (pos != std::string::npos) ? path.substr(pos + 1) : path;
}

// Try to extract exception message if it's a std::exception
std::string try_get_exception_message(void* thrown_exception, std::type_info* tinfo) {
    try {
        if (thrown_exception && tinfo) {
            // This is hacky but sometimes works for std::exception derivatives
            auto* e = static_cast<std::exception*>(thrown_exception);
            if (e) {
                const char* msg = e->what();
                if (msg) {
                    return std::string(msg);
                }
            }
        }
    } catch (...) {
        // Ignore any errors in trying to get the message
    }
    return "";
}

} // anonymous namespace

// Backtrace::Frame implementation
std::string Backtrace::Frame::to_string() const {
    std::ostringstream oss;
    
    oss << "  #" << std::setw(2) << index << " ";
    
    if (has_source_info()) {
        // Format: function at file:line
        oss << function_name << " at " << source_file << ":" << line_number;
    } else if (!function_name.empty()) {
        oss << function_name;
        if (!offset.empty()) {
            oss << " " << offset;
        }
        if (!module_name.empty()) {
            oss << " (" << module_name << ")";
        }
    } else if (!raw_symbol.empty()) {
        oss << raw_symbol;
    } else {
        oss << "[" << address << "]";
    }
    
    return oss.str();
}

// Backtrace implementation
void Backtrace::capture(void* thrown_exception, std::type_info* tinfo) {
    clear();
    
    // Capture exception info if provided
    if (tinfo) {
        exception_info.type_name = demangle(tinfo->name());
        exception_info.what_message = try_get_exception_message(thrown_exception, tinfo);
    }
    
    // Capture stack
    void* array[100];
    int size = backtrace(array, 100);
    
    if (size <= 0) {
        return;
    }
    
    char** symbols = backtrace_symbols(array, size);
    if (!symbols) {
        return;
    }
    
    // Try to get the main executable path for addr2line
    std::string main_executable;
    Dl_info main_info;
    if (dladdr(array[0], &main_info) && main_info.dli_fname) {
        main_executable = main_info.dli_fname;
    }
    
    // Start from 1 to skip __wrap___cxa_throw itself
    for (int i = 1; i < size; ++i) {
        Frame frame;
        frame.index = i;
        frame.address = array[i];
        frame.raw_symbol = symbols[i];
        
        // Parse the raw symbol
        SymbolInfo sym_info(symbols[i]);
        
        // Try to get detailed info via dladdr
        Dl_info info;
        bool has_dlinfo = dladdr(array[i], &info);
        
        // First, try addr2line for file:line info
        bool addr2line_success = false;
        if (!main_executable.empty()) {
            Addr2LineResult addr_result = addr2line(main_executable, array[i]);
            if (addr_result.success) {
                frame.function_name = addr_result.function;
                frame.source_file = addr_result.file;
                frame.line_number = addr_result.line;
                frame.module_name = basename(main_executable);
                addr2line_success = true;
            }
        }
        
        // If addr2line didn't work, use dladdr info
        if (!addr2line_success) {
            if (has_dlinfo) {
                if (info.dli_sname) {
                    frame.function_name = demangle(info.dli_sname);
                    
                    // Calculate offset
                    if (info.dli_saddr) {
                        auto offset = static_cast<char*>(array[i]) - static_cast<char*>(info.dli_saddr);
                        if (offset != 0) {
                            std::ostringstream oss;
                            oss << "+ 0x" << std::hex << offset;
                            frame.offset = oss.str();
                        }
                    }
                }
                
                if (info.dli_fname) {
                    frame.module_name = basename(info.dli_fname);
                }
            } else if (!sym_info.symbol.empty()) {
                // Use parsed symbol info
                frame.function_name = demangle(sym_info.symbol.c_str());
                frame.offset = sym_info.offset;
                if (!sym_info.module.empty()) {
                    frame.module_name = basename(sym_info.module);
                }
            }
        }
        
        frames.push_back(frame);
    }
    
    free(symbols);
}

std::string Backtrace::to_string() const {
    if (frames.empty()) {
        return "[No backtrace available]\n";
    }
    
    std::ostringstream oss;
    
    // Header
    oss << "\n" << std::string(80, '=') << "\n";
    
    if (!exception_info.type_name.empty()) {
        oss << "EXCEPTION: " << exception_info.type_name << "\n";
        
        if (!exception_info.what_message.empty()) {
            oss << "WHAT: " << exception_info.what_message << "\n";
        }
        
        // Format timestamp
        auto time_t = std::chrono::system_clock::to_time_t(exception_info.timestamp);
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            exception_info.timestamp.time_since_epoch()) % 1000;
        
        char time_buf[100];
        std::strftime(time_buf, sizeof(time_buf), "%Y-%m-%d %H:%M:%S", 
                      std::localtime(&time_t));
        
        oss << "TIME: " << time_buf << "." << std::setfill('0') << std::setw(3) << ms.count() << "\n";
        oss << "THREAD: 0x" << std::hex << exception_info.thread_id << std::dec << "\n";
        oss << std::string(80, '-') << "\n";
    }
    
    oss << "BACKTRACE:\n";
    
    for (const auto& frame : frames) {
        oss << frame.to_string() << "\n";
    }
    
    oss << std::string(80, '=') << "\n";
    
    return oss.str();
}

std::string Backtrace::to_simple_string() const {
    std::ostringstream oss;
    for (const auto& frame : frames) {
        if (!frame.function_name.empty()) {
            oss << frame.function_name << "\n";
        }
    }
    return oss.str();
}

std::vector<Backtrace::Frame> Backtrace::filter_by_module(const std::string& module_pattern) const {
    std::vector<Frame> filtered;
    for (const auto& frame : frames) {
        if (frame.module_name.find(module_pattern) != std::string::npos) {
            filtered.push_back(frame);
        }
    }
    return filtered;
}

std::vector<Backtrace::Frame> Backtrace::with_source_info() const {
    std::vector<Frame> filtered;
    for (const auto& frame : frames) {
        if (frame.has_source_info()) {
            filtered.push_back(frame);
        }
    }
    return filtered;
}

extern "C" {
    void __real___cxa_throw(void*, std::type_info*, void(*)(void*));

    void __wrap___cxa_throw(void* thrown_exception, std::type_info* tinfo, void(*destructor)(void*)) {
        // Capture the backtrace
        g_last_throw_backtrace.capture(thrown_exception, tinfo);
        
        // Optionally print immediately for debugging
        // Uncomment the next line if you want immediate output
        // std::cerr << g_last_throw_backtrace.to_string() << std::flush;
        
        // Call the real throw
        __real___cxa_throw(thrown_exception, tinfo, destructor);
    }
}

} // namespace autocog::utilities
