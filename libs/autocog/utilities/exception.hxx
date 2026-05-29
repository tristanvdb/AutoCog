#ifndef AUTOCOG_UTILITIES_HXX
#define AUTOCOG_UTILITIES_HXX

#include "autocog/utilities/errors.hxx"
#include "autocog/logging.hxx"

#include <string>
#include <iostream>
#include <optional>
#include <functional>
#include <vector>
#include <chrono>
#include <thread>
#include <sstream>

namespace autocog::utilities {

// Forward declaration
struct Backtrace;

struct InternalError : autocog::AutoCogError {
  InternalError(std::string msg);
};

// Structured backtrace information
struct Backtrace {
  struct Frame {
    int index;                    // Frame number
    void* address;                // Raw address
    std::string function_name;    // Demangled function name
    std::string module_name;      // Library/executable name
    std::string source_file;      // Source file (if available)
    int line_number;              // Line number (if available)
    std::string offset;           // Offset from function start
    std::string raw_symbol;       // Original symbol (fallback)
    
    Frame() : index(0), address(nullptr), line_number(-1) {}
    
    // Check if we have source location info
    bool has_source_info() const {
      return !source_file.empty() && line_number > 0;
    }
    
    // Format a single frame
    std::string to_string() const;
  };
  
  // Exception information
  struct ExceptionInfo {
    std::string type_name;        // Demangled exception type
    std::string what_message;     // Exception what() if available
    std::thread::id thread_id;    // Thread that threw
    std::chrono::system_clock::time_point timestamp;
    
    ExceptionInfo() : thread_id(std::this_thread::get_id()), 
                      timestamp(std::chrono::system_clock::now()) {}
  };
  
  ExceptionInfo exception_info;
  std::vector<Frame> frames;
  
  // Capture the current backtrace
  void capture(void* thrown_exception = nullptr, std::type_info* tinfo = nullptr);
  
  // Format the entire backtrace as a string
  std::string to_string() const;
  
  // Get a simplified format (just function names)
  std::string to_simple_string() const;
  
  // Get only frames from a specific module
  std::vector<Frame> filter_by_module(const std::string& module_pattern) const;
  
  // Get only frames with source info
  std::vector<Frame> with_source_info() const;
  
  // Check if backtrace is empty
  bool empty() const { return frames.empty(); }
  
  // Get number of frames
  size_t size() const { return frames.size(); }
  
  // Clear the backtrace
  void clear() { 
    frames.clear(); 
    exception_info = ExceptionInfo();
  }
};

// Global thread-local storage for last exception backtrace
extern thread_local Backtrace g_last_throw_backtrace;

extern "C" {
  void __cxa_throw(void* thrown_exception, std::type_info* tinfo, void(*destructor)(void*));
}

// Wrap a CLI tool's body so that an uncaught exception is logged (and, in
// Debug builds, accompanied by the backtrace captured at throw time by the
// __cxa_throw hook below). Returns the callable's int result on success, or a
// non-zero code if it threw. This is for the standalone C++ tools; the Python
// bindings translate exceptions via their own translator instead.
template <typename Callable, typename... Args>
int guard_main(Callable&& callable, Args&&... args) {
  auto log_backtrace = []() {
#ifndef NDEBUG
    if (!g_last_throw_backtrace.empty()) {
      SPDLOG_LOGGER_DEBUG(autocog::log(), "backtrace:\n{}",
                          g_last_throw_backtrace.to_string());
    }
#endif
  };
  try {
    return std::invoke(std::forward<Callable>(callable),
                       std::forward<Args>(args)...);
  } catch (autocog::AutoCogError const & e) {
    if (dynamic_cast<InternalError const *>(&e)) {
      SPDLOG_LOGGER_CRITICAL(autocog::log(), "internal error: {}", e.what());
    } else {
      SPDLOG_LOGGER_ERROR(autocog::log(), "{}", e.what());
    }
    log_backtrace();
    return dynamic_cast<InternalError const *>(&e) ? 250 : 1;
  } catch (std::exception const & e) {
    SPDLOG_LOGGER_CRITICAL(autocog::log(), "uncaught: {} ({})", e.what(), typeid(e).name());
    log_backtrace();
    return 251;
  } catch (...) {
    SPDLOG_LOGGER_CRITICAL(autocog::log(), "uncaught unknown exception");
    log_backtrace();
    return 252;
  }
}

} // namespace autocog::utilities

#endif /* AUTOCOG_UTILITIES_HXX */
