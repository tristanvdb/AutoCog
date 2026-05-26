#ifndef AUTOCOG_BUILD_INFO_HXX
#define AUTOCOG_BUILD_INFO_HXX

#include <string>
#include <sstream>

namespace autocog {

inline std::string version() {
    return AUTOCOG_VERSION;
}

inline std::string build_info() {
    std::ostringstream out;
    out << "autocog " << AUTOCOG_VERSION << "\n"
        << "  build_type:  " << AUTOCOG_BUILD_TYPE << "\n"
        << "  compiler:    " << AUTOCOG_COMPILER_ID << " " << AUTOCOG_COMPILER_VERSION << "\n"
        << "  system:      " << AUTOCOG_SYSTEM << "\n"
        << "  llama:       " << AUTOCOG_LLAMA_SOURCE << "\n"
        << "  cuda:        " << (AUTOCOG_CUDA ? "yes" : "no") << "\n"
        << "  native:      " << (AUTOCOG_NATIVE ? "yes" : "no") << "\n";
    return out.str();
}

}

#endif // AUTOCOG_BUILD_INFO_HXX
