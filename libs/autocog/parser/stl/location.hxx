#ifndef AUTOCOG_PARSER_STL_LOCATION_HXX
#define AUTOCOG_PARSER_STL_LOCATION_HXX

namespace autocog::parser {

// Source location tracking
struct SourceLocation {
    int line;
    int column;
    unsigned offset;
};

}

#endif /* AUTOCOG_PARSER_STL_LOCATION_HXX */
