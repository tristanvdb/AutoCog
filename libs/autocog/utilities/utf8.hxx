#ifndef AUTOCOG_UTILITIES_UTF8_HXX
#define AUTOCOG_UTILITIES_UTF8_HXX

#include <string>

namespace autocog::utilities {

/// Length of the valid UTF-8 sequence starting at `bytes[pos]`, strict per
/// the WHATWG table (rejects overlongs, surrogates, > U+10FFFF). Returns 0
/// for an invalid sequence, and -(expected length) when the string ends
/// mid-sequence but every byte so far was a valid prefix (an incomplete
/// tail — typically a token budget cutting a multi-byte character short).
inline int utf8_sequence(std::string const & bytes, std::size_t pos) {
  auto u = [&](std::size_t i) { return static_cast<unsigned char>(bytes[i]); };
  std::size_t const n = bytes.size();
  unsigned char const c0 = u(pos);
  if (c0 < 0x80) return 1;
  int len;
  unsigned char lo = 0x80, hi = 0xBF;   // bounds for the *first* continuation
  if      (c0 >= 0xC2 && c0 <= 0xDF) { len = 2; }
  else if (c0 == 0xE0)               { len = 3; lo = 0xA0; }
  else if (c0 >= 0xE1 && c0 <= 0xEC) { len = 3; }
  else if (c0 == 0xED)               { len = 3; hi = 0x9F; }
  else if (c0 >= 0xEE && c0 <= 0xEF) { len = 3; }
  else if (c0 == 0xF0)               { len = 4; lo = 0x90; }
  else if (c0 >= 0xF1 && c0 <= 0xF3) { len = 4; }
  else if (c0 == 0xF4)               { len = 4; hi = 0x8F; }
  else return 0;
  for (int i = 1; i < len; ++i) {
    if (pos + i >= n) return -len;
    unsigned char const c = u(pos + i);
    if (c < (i == 1 ? lo : 0x80) || c > (i == 1 ? hi : 0xBF)) return 0;
  }
  return len;
}

/// Make `bytes` valid UTF-8: each invalid byte becomes U+FFFD, and an
/// incomplete-but-so-far-valid trailing sequence is dropped (the partial
/// character was never completed, so nothing meaningful is lost). Valid
/// input passes through unchanged.
inline std::string utf8_sanitize(std::string const & bytes) {
  std::string out;
  out.reserve(bytes.size());
  std::size_t pos = 0;
  while (pos < bytes.size()) {
    int const len = utf8_sequence(bytes, pos);
    if (len > 0) {
      out.append(bytes, pos, static_cast<std::size_t>(len));
      pos += static_cast<std::size_t>(len);
    } else if (len < 0) {
      break;                  // incomplete tail: drop
    } else {
      out += "\xEF\xBF\xBD";  // U+FFFD replacement character
      pos += 1;
    }
  }
  return out;
}

}

#endif // AUTOCOG_UTILITIES_UTF8_HXX
