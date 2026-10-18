// Standalone assert-trigger program for thermo::espidf_cpp::static_string.
//
// The Unity tests (test_static_string.cpp) fork+exec this program with a case
// name and confirm the child aborts. Keeping the triggers in a separate program
// means an abort cannot take down the test runner.
//
// Usage: static_string_assert <case-name>
//   * A valid case name runs the corresponding trigger, which aborts.
//   * A missing or unknown case name exits 0 (nothing to execute), so the
//     forking test can detect a *clean* exit as "the assert did not fire".
#include "assert_test_support.hpp"
#include "static_string_assert_cases.hpp"

int main(int argc, char** argv) {
    return assert_test_support::run_assert_program(
        argc, argv, static_string_assert_cases::cases());
}
