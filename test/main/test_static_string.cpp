// Tests for the fixed-capacity NUL-terminated string (thermo::static_string).
//
// static_string wraps thermo::static_vector<char, N + 1> and guarantees a
// valid NUL terminator, so c_str() is always safe. It is pure logic with no
// IDF dependencies and is covered on the host. Overflow asserts (a programming
// error) rather than truncating, so the boundary cases below exercise exact-fit
// operations only. The asserting overflow paths are verified in forked children
// that exec the standalone `static_string_assert` program (see the [assert]
// cases at the end).
#include "unity.h"

#include <sys/wait.h>
#include <unistd.h>
#include <cerrno>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include "static_string_assert_cases.hpp"
#include "thermo_static_string.hpp"

using thermo::static_string;

namespace {

// Absolute path to the standalone assert program. It is emitted next to the
// test ELF, so resolve this process's own directory via /proc/self/exe.
std::string assert_program_path() {
    char exe[4096];
    const ssize_t n = readlink("/proc/self/exe", exe, sizeof(exe) - 1);
    TEST_ASSERT_TRUE_MESSAGE(n > 0, "readlink(/proc/self/exe) failed");
    exe[n] = '\0';
    std::string path(exe);
    const std::size_t slash = path.find_last_of('/');
    path.resize(slash == std::string::npos ? 0 : slash + 1);
    return path + "static_string_assert";
}

// Forks, execs the standalone assert program with `case_name`, and waits for
// it. Returns true iff the child terminated abnormally, i.e. the assert fired.
bool assert_program_aborted(const std::string& program, const char* case_name) {
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

}  // namespace

// --- Construction and capacity ----------------------------------------------

TEST_CASE("static_string: default is empty and NUL-terminated",
          "[static_string]") {
    static_string<8> s;
    TEST_ASSERT_EQUAL_UINT(0, s.size());
    TEST_ASSERT_TRUE(s.empty());
    TEST_ASSERT_EQUAL_UINT(8, s.capacity());
    TEST_ASSERT_EQUAL_STRING("", s.c_str());
    TEST_ASSERT_NOT_NULL(s.data());
}

TEST_CASE("static_string: construct from a C string", "[static_string]") {
    static_string<8> s("hello");
    TEST_ASSERT_EQUAL_UINT(5, s.size());
    TEST_ASSERT_FALSE(s.empty());
    TEST_ASSERT_EQUAL_STRING("hello", s.c_str());
}

TEST_CASE("static_string: a null C string yields an empty string",
          "[static_string]") {
    static_string<8> s(static_cast<const char*>(nullptr));
    TEST_ASSERT_EQUAL_UINT(0, s.size());
    TEST_ASSERT_EQUAL_STRING("", s.c_str());
}

// --- c_str always NUL-terminated --------------------------------------------

TEST_CASE("static_string: c_str is NUL-terminated at size", "[static_string]") {
    static_string<8> s("abc");
    TEST_ASSERT_EQUAL_CHAR('\0', s.c_str()[3]);
    TEST_ASSERT_EQUAL_CHAR('\0', s.data()[s.size()]);
    TEST_ASSERT_EQUAL_UINT(3, std::strlen(s.c_str()));
}

// --- Exact-fit boundary -----------------------------------------------------

TEST_CASE("static_string: exactly-capacity construction fits",
          "[static_string]") {
    static_string<5> s("abcde");
    TEST_ASSERT_EQUAL_UINT(5, s.size());
    TEST_ASSERT_EQUAL_STRING("abcde", s.c_str());
    TEST_ASSERT_EQUAL_CHAR('\0', s.c_str()[5]);
    TEST_ASSERT_EQUAL_UINT(0, s.remaining());
}

// --- append / assign --------------------------------------------------------

TEST_CASE("static_string: append concatenates", "[static_string]") {
    static_string<10> s("foo");
    s.append("bar");
    TEST_ASSERT_EQUAL_UINT(6, s.size());
    TEST_ASSERT_EQUAL_STRING("foobar", s.c_str());
}

TEST_CASE("static_string: append fills exactly to capacity",
          "[static_string]") {
    static_string<5> s("abc");
    s.append("de");  // Fits exactly: remaining() == 2.
    TEST_ASSERT_EQUAL_UINT(5, s.size());
    TEST_ASSERT_EQUAL_STRING("abcde", s.c_str());
    TEST_ASSERT_EQUAL_UINT(0, s.remaining());
}

TEST_CASE("static_string: append a null pointer is a no-op",
          "[static_string]") {
    static_string<8> s("keep");
    s.append(nullptr);
    TEST_ASSERT_EQUAL_STRING("keep", s.c_str());
}

TEST_CASE("static_string: assign replaces the contents", "[static_string]") {
    static_string<8> s("original");
    s.assign("new");
    TEST_ASSERT_EQUAL_UINT(3, s.size());
    TEST_ASSERT_EQUAL_STRING("new", s.c_str());
}

TEST_CASE("static_string: assign accepts an exactly-capacity string",
          "[static_string]") {
    static_string<7> s("x");
    s.assign("toolong");  // 7 characters, exactly capacity().
    TEST_ASSERT_EQUAL_UINT(7, s.size());
    TEST_ASSERT_EQUAL_STRING("toolong", s.c_str());
    TEST_ASSERT_EQUAL_UINT(0, s.remaining());
}

// --- push_back / clear ------------------------------------------------------

TEST_CASE("static_string: push_back appends single characters",
          "[static_string]") {
    static_string<4> s;
    s.push_back('h');
    s.push_back('i');
    TEST_ASSERT_EQUAL_UINT(2, s.size());
    TEST_ASSERT_EQUAL_STRING("hi", s.c_str());
}

TEST_CASE("static_string: push_back fills exactly to capacity",
          "[static_string]") {
    static_string<2> s;
    s.push_back('a');
    s.push_back('b');  // Fills the last free slot.
    TEST_ASSERT_EQUAL_UINT(2, s.size());
    TEST_ASSERT_EQUAL_STRING("ab", s.c_str());
    TEST_ASSERT_EQUAL_UINT(0, s.remaining());
}

TEST_CASE("static_string: clear leaves an empty terminated string",
          "[static_string]") {
    static_string<8> s("data");
    s.clear();
    TEST_ASSERT_EQUAL_UINT(0, s.size());
    TEST_ASSERT_EQUAL_STRING("", s.c_str());
}

// --- Element access and iteration -------------------------------------------

TEST_CASE("static_string: indexing and iteration expose characters",
          "[static_string]") {
    static_string<8> s("abc");
    TEST_ASSERT_EQUAL_CHAR('a', s[0]);
    TEST_ASSERT_EQUAL_CHAR('c', s[2]);

    int count = 0;
    for (auto it = s.cbegin(); it != s.cend(); ++it) {
        ++count;
    }
    TEST_ASSERT_EQUAL_INT(3, count);
    TEST_ASSERT_EQUAL_PTR(s.begin(), s.data());
    TEST_ASSERT_EQUAL_PTR(s.cend(), s.data() + s.size());
}

// --- capacity()/remaining() -------------------------------------------------

TEST_CASE("static_string: remaining tracks free character slots",
          "[static_string]") {
    static_string<6> s("abc");
    TEST_ASSERT_EQUAL_UINT(3, s.remaining());
    s.append("def");
    TEST_ASSERT_EQUAL_UINT(0, s.remaining());
    TEST_ASSERT_TRUE(s.size() == s.capacity());
}

// --- Overflow asserts (fork + exec the standalone program) ------------------

TEST_CASE("static_string: constructing past capacity aborts",
          "[static_string][assert]") {
    TEST_ASSERT_TRUE(
        assert_program_aborted(assert_program_path(), "construct_overflow"));
}

TEST_CASE("static_string: append past capacity aborts",
          "[static_string][assert]") {
    TEST_ASSERT_TRUE(
        assert_program_aborted(assert_program_path(), "append_overflow"));
}

TEST_CASE("static_string: assign past capacity aborts",
          "[static_string][assert]") {
    TEST_ASSERT_TRUE(
        assert_program_aborted(assert_program_path(), "assign_overflow"));
}

TEST_CASE("static_string: push_back when full aborts",
          "[static_string][assert]") {
    TEST_ASSERT_TRUE(
        assert_program_aborted(assert_program_path(), "push_back_when_full"));
}
