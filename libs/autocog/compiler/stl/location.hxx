#ifndef AUTOCOG_COMPILER_STL_LOCATION_HXX
#define AUTOCOG_COMPILER_STL_LOCATION_HXX

namespace autocog::compiler::stl {

struct SourceLocation {
  int fid;
  int line;
  int column;
  unsigned offset;
};

struct SourceRange {
  SourceLocation start;
  SourceLocation stop;
};

}

#endif /* AUTOCOG_COMPILER_STL_LOCATION_HXX */
