#ifndef AUTOCOG_COMPILER_STL_LOCATION_HXX
#define AUTOCOG_COMPILER_STL_LOCATION_HXX

namespace autocog::compiler::stl {

// Source location tracking
struct SourceLocation {
    int line;
    int column;
    unsigned offset;
};

}

#endif /* AUTOCOG_COMPILER_STL_LOCATION_HXX */
