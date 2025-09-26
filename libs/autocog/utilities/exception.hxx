#ifndef AUTOCOG_UTILITIES_HXX
#define AUTOCOG_UTILITIES_HXX

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

struct InternalError : std::exception {
  std::string message;
  
  InternalError(std::string msg);
  
  const char * what() const noexcept override;
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

template <typename Callable, typename... Args>
std::optional<int> wrap_exception(Callable&& callable, Args&&... args) {
  try {
    return std::invoke(std::forward<Callable>(callable), 
                       std::forward<Args>(args)...);

  } catch (InternalError const & e) {
    std::cerr << "Internal error: " << e.what() << "\n";
    if (!g_last_throw_backtrace.empty()) {
      std::cerr << g_last_throw_backtrace.to_string() << "\n";
    }
    return 250;

  } catch (std::exception const & e) {
    std::cerr << "Uncaught exception: " << e.what() << "\n";
    if (!g_last_throw_backtrace.empty()) {
      std::cerr << g_last_throw_backtrace.to_string() << "\n";
    }
    return 251;

  } catch (...) {
    std::cerr << "Uncaught unknown exception\n";
    if (!g_last_throw_backtrace.empty()) {
      std::cerr << g_last_throw_backtrace.to_string() << "\n";
    }
    return 252;
  }
}

} // namespace autocog::utilities

#endif /* AUTOCOG_UTILITIES_HXX */
