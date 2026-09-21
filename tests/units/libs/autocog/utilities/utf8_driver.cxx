// utf8_sanitize is the safety net between token-level byte streams and every
// text consumer (JSON dump, py::str): byte-fallback tokens let a completion's
// token budget cut a multi-byte character short, and a real model can emit
// arbitrary invalid byte runs. Sanitized output must always be strict UTF-8,
// valid input must pass through untouched, and an incomplete trailing
// sequence must be dropped (not replaced — the character never completed).

#include "autocog/utilities/utf8.hxx"

#include <iostream>
#include <string>

using autocog::utilities::utf8_sanitize;

static int failures = 0;
static void check(std::string const & input, std::string const & expected,
                  std::string const & what) {
    std::string const got = utf8_sanitize(input);
    if (got == expected) {
        std::cout << "  ok: " << what << "\n";
    } else {
        std::cout << "  FAIL: " << what << " (got " << got.size()
                  << " bytes, expected " << expected.size() << ")\n";
        ++failures;
    }
}

int main() {
    std::string const rep = "\xEF\xBF\xBD";  // U+FFFD

    // Valid input passes through unchanged.
    check("", "", "empty");
    check("hello world", "hello world", "ascii");
    check("h\xC3\xA9llo", "h\xC3\xA9llo", "2-byte (e-acute)");
    check("\xE2\x82\xAC", "\xE2\x82\xAC", "3-byte (euro sign)");
    check("\xF0\x9F\x99\x82", "\xF0\x9F\x99\x82", "4-byte (emoji)");

    // Incomplete trailing sequences are dropped (budget-cut character).
    check("abc\xC3", "abc", "truncated 2-byte tail dropped");
    check("abc\xE2\x82", "abc", "truncated 3-byte tail dropped");
    check("abc\xF0\x9F\x99", "abc", "truncated 4-byte tail dropped");

    // Invalid bytes become U+FFFD; scanning resumes on the next byte.
    check("\x80", rep, "lone continuation byte");
    check("a\xC3xb", "a" + rep + "xb", "interior truncated sequence");
    check("\xC0\x80", rep + rep, "overlong encoding rejected");
    check("\xED\xA0\x80", rep + rep + rep, "surrogate rejected");
    check("\xF4\x90\x80\x80", rep + rep + rep + rep, "> U+10FFFF rejected");
    check("\xFE\xFF", rep + rep, "invalid lead bytes");

    // Edge combinations.
    check("\xC3\xA9\xC3", "\xC3\xA9", "valid pair then truncated tail");
    check("\xE0\x9F\x80", rep + rep + rep, "3-byte overlong (E0 9F) rejected");

    if (failures) { std::cout << failures << " failure(s)\n"; return 1; }
    std::cout << "all ok\n";
    return 0;
}
