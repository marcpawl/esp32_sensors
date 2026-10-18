// Shared infrastructure for the standalone `_assert` trigger programs.
//
// A trigger program (filename containing "_assert") takes a case name as its
// only argument and runs the corresponding trigger, which must abort. The Unity
// tests fork+exec it and confirm the child aborted, so a contract violation
// never takes down the test runner itself.
//
// This header is deliberately free of any Unity dependency (the trigger
// programs are plain host executables that do not link Unity). The test-side
// helpers that need Unity live in assert_unity_support.hpp.
#pragma once

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <span>

namespace assert_test_support {

// One trigger per assert path. Each must abort when executed.
using Trigger = void (*)();

// A named trigger; the name is the command-line argument that selects it.
struct Case {
    const char* name;
    Trigger trigger;
};

// Common `main` body for a trigger program.
//
// With no argument, or an unknown case name, returns 0 (nothing to execute) so
// the forking test can treat a clean exit as "the assert did not fire".
// Otherwise runs the matching trigger, which must abort; it only returns 0 if
// the trigger failed to abort (reported on stderr).
inline int run_assert_program(int argc, char** argv,
                              std::span<const Case> cases) {
    if (argc < 2) {
        // No case requested: success, nothing to do.
        return 0;
    }

    const char* requested = argv[1];
    const auto it = std::ranges::find_if(cases, [requested](const Case& c) {
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

}  // namespace assert_test_support
