// Standalone assert-trigger program for thermo::static_string.
//
// The Unity tests (test_static_string.cpp) fork+exec this program with a case
// name and confirm the child aborts. Keeping the triggers in a separate program
// means an abort cannot take down the test runner.
//
// Usage: static_string_assert <case-name>
//   * A valid case name runs the corresponding trigger, which aborts.
//   * A missing or unknown case name exits 0 (nothing to execute), so the
//     forking test can detect a *clean* exit as "the assert did not fire".
#include <algorithm>
#include <cstdio>
#include <cstring>

#include "static_string_assert_cases.hpp"

int main(int argc, char** argv) {
    if (argc < 2) {
        // No case requested: success, nothing to do.
        return 0;
    }

    const char* requested = argv[1];
    const auto& cases = static_string_assert_cases::cases();
    const auto it = std::ranges::find_if(cases, [requested](const auto& c) {
        return std::strcmp(requested, c.name) == 0;
    });
    if (it == cases.end()) {
        // Unknown case name: treat as a no-op success.
        return 0;
    }

    it->trigger();  // Must abort.
    // Reached only if the trigger failed to abort.
    std::fprintf(stderr, "case '%s' did not abort\n", requested);
    return 0;
}
