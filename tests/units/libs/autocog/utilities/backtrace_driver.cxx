// Exercises the exception/backtrace machinery so we know the crash-time safety
// net actually works: capturing a backtrace and formatting it must not itself
// fault, and the --wrap=__cxa_throw hook must populate the thread-local
// backtrace when an exception is thrown.

#include "autocog/utilities/exception.hxx"
#include "autocog/utilities/errors.hxx"

#include <iostream>
#include <string>

using autocog::utilities::Backtrace;
using autocog::utilities::InternalError;

static int failures = 0;
static void check(bool cond, std::string const & what) {
    if (cond) {
        std::cout << "  ok: " << what << "\n";
    } else {
        std::cout << "  FAIL: " << what << "\n";
        ++failures;
    }
}

int main() {
    // 1. Direct capture in this frame.
    {
        Backtrace bt;
        bt.capture();
        check(!bt.empty(), "capture() produced frames");
        check(bt.size() > 0, "size() > 0");

        // Formatting must not fault and must produce text.
        std::string full = bt.to_string();
        std::string simple = bt.to_simple_string();
        check(!full.empty(), "to_string() non-empty");
        check(!simple.empty(), "to_simple_string() non-empty");

        // Frame formatting.
        bool any_frame_formats = false;
        for (auto const & f : bt.frames) {
            if (!f.to_string().empty()) { any_frame_formats = true; break; }
        }
        check(any_frame_formats, "at least one Frame::to_string() non-empty");

        // Filters must run without faulting.
        auto with_src = bt.with_source_info();
        auto by_mod = bt.filter_by_module("autocog");
        check(with_src.size() <= bt.size(), "with_source_info() is a subset");
        check(by_mod.size() <= bt.size(), "filter_by_module() is a subset");

        // clear() resets.
        bt.clear();
        check(bt.empty(), "clear() empties the backtrace");
    }

    // 2. The __cxa_throw hook must populate g_last_throw_backtrace on throw.
    {
        autocog::utilities::g_last_throw_backtrace.clear();
        bool caught = false;
        try {
            throw InternalError("test internal error");
        } catch (autocog::AutoCogError const & e) {
            caught = true;
            check(std::string(e.what()).find("test internal error") != std::string::npos,
                  "thrown InternalError carries its message");
        }
        check(caught, "InternalError was caught");
#ifndef NDEBUG
        // In Debug builds the throw hook captures a backtrace.
        check(!autocog::utilities::g_last_throw_backtrace.empty(),
              "throw hook populated g_last_throw_backtrace (Debug)");
        check(!autocog::utilities::g_last_throw_backtrace.to_string().empty(),
              "captured throw backtrace formats");
#endif
    }

    std::cout << (failures == 0 ? "ALL PASSED\n" : "FAILURES\n");
    return failures == 0 ? 0 : 1;
}
