#ifndef AUTOCOG_DATA_UTILITY_HXX
#define AUTOCOG_DATA_UTILITY_HXX

#include <cstdint>
#include <cstring>
#include <optional>
#include <string>
#include <vector>

namespace autocog::data {

/// Current UTC time as an RFC 3339 string (e.g. "2026-06-04T22:31:18Z").
std::string utc_timestamp();

/// SHA-256 of @p input as a 64-character lowercase hex string.
std::string digest(std::string const & input);

/// Accumulates field values into a canonical buffer for content hashing.
///
/// Fields are appended in declaration order, each followed by a '\0'
/// separator; field *names* are intentionally excluded to minimize the hashed
/// data. Values are assumed not to contain '\0' (to be enforced by input
/// validation); embedded nulls would reintroduce ambiguity between adjacent
/// fields.
class ContentHasher {
  public:
    ContentHasher & put(std::string const & s) { buf_ += s; buf_ += '\0'; return *this; }
    ContentHasher & put(bool b)                { buf_ += (b ? '1' : '0'); buf_ += '\0'; return *this; }
    ContentHasher & put(unsigned u)            { buf_ += std::to_string(u); buf_ += '\0'; return *this; }
    ContentHasher & put(std::int32_t i)        { buf_ += std::to_string(i); buf_ += '\0'; return *this; }

    /// Floats are hashed by their exact IEEE-754 bit pattern (8 hex chars):
    /// equality is bit-exact, with no rounding or precision normalization.
    ContentHasher & put(float f) {
      std::uint32_t bits = 0;
      std::memcpy(&bits, &f, sizeof bits);
      static char const hexd[] = "0123456789abcdef";
      for (int i = 7; i >= 0; --i) buf_ += hexd[(bits >> (i * 4)) & 0xfu];
      buf_ += '\0';
      return *this;
    }

    /// Sequence: a size prefix, then each element.
    template <class T>
    ContentHasher & put(std::vector<T> const & v) {
      put(static_cast<unsigned>(v.size()));
      for (auto const & e : v) put(e);
      return *this;
    }

    /// Optional: a presence flag, followed by the value only when present.
    template <class T>
    ContentHasher & put(std::optional<T> const & o) {
      put(o.has_value());
      if (o) put(*o);
      return *this;
    }

    std::string hash() const { return digest(buf_); }

  private:
    std::string buf_;
};

}

#endif // AUTOCOG_DATA_UTILITY_HXX
