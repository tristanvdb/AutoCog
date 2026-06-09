#ifndef AUTOCOG_UTILITIES_LOCATION_HXX
#define AUTOCOG_UTILITIES_LOCATION_HXX

namespace autocog::location {

/// A point in a source file: which file (fid), and line/column/byte offset.
/// fid indexes the per-compilation file table owned by the Driver.
struct SourceLocation {
  int fid;
  int line;
  int column;
  unsigned offset;
};

/// A half-open span between two points in the same source file.
struct SourceRange {
  SourceLocation start;
  SourceLocation stop;
};

}

#endif /* AUTOCOG_UTILITIES_LOCATION_HXX */
