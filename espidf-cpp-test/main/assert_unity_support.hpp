// Unity-side helpers for the standalone `_assert` trigger programs.
//
// These wrap fork+exec of a sibling trigger program and observe whether it
// aborted. They are separated from assert_test_support.hpp because they depend
// on Unity's TEST_ASSERT_* macros, which the trigger programs themselves do not
// (and must not) link.
#pragma once

#include "unity.h"

#include <cerrno>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <string>

#include <sys/wait.h>
#include <unistd.h>

namespace assert_test_support {

// Absolute path to a sibling `_assert` program. The program is emitted next to
// the test ELF, so resolve this process's own directory via /proc/self/exe.
inline std::string assert_program_path(const char* program_name) {
    char exe[4096];
    const ssize_t n = readlink("/proc/self/exe", exe, sizeof(exe) - 1);
    TEST_ASSERT_TRUE_MESSAGE(n > 0, "readlink(/proc/self/exe) failed");
    exe[n] = '\0';
    std::string path(exe);
    const std::size_t slash = path.find_last_of('/');
    path.resize(slash == std::string::npos ? 0 : slash + 1);
    return path + program_name;
}

// Forks, execs `program` with `case_name`, and waits for it. Returns true iff
// the child terminated abnormally, i.e. the assert fired.
inline bool assert_program_aborted(const std::string& program,
                                   const char* case_name) {
    const pid_t pid = fork();
    TEST_ASSERT_TRUE_MESSAGE(pid >= 0, "fork() failed");
    if (pid == 0) {
        // Child: run the trigger in a fresh process so its abort cannot take
        // down the test runner.
        execl(program.c_str(), program.c_str(), case_name,
              static_cast<char*>(nullptr));
        // execl only returns on failure.
        _exit(127);
    }

    // The parent is a multi-threaded FreeRTOS/linux process, so the wait may be
    // interrupted by a signal; retry until the child is actually reaped.
    int status = 0;
    pid_t waited;
    do {
        waited = waitpid(pid, &status, 0);
    } while (waited == -1 && errno == EINTR);
    TEST_ASSERT_EQUAL_INT(pid, waited);

    // NOTE: on the ESP-IDF `linux` target, the assert handler prints
    // "ERROR: Aborted" and calls _exit(1), so the child may exit non-zero
    // rather than die from SIGABRT. Accept either form; a clean 0 means the
    // assert did not fire (e.g. an unknown case name).
    if (WIFSIGNALED(status)) {
        return WTERMSIG(status) == SIGABRT;
    }
    return WIFEXITED(status) && WEXITSTATUS(status) != 0;
}

}  // namespace assert_test_support
