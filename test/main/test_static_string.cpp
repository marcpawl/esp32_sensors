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

#include <cstddef>
#include <cstring>
#include <string>

#include "assert_unity_support.hpp"
#include "thermo_static_string.hpp"

using thermo::static_string;

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

namespace {

// Path to the sibling static_string_assert program (shared by the cases below).
const std::string& assert_program() {
    static const std::string path =
        assert_test_support::assert_program_path("static_string_assert");
    return path;
}

}  // namespace

TEST_CASE("static_string: constructing past capacity aborts",
          "[static_string][assert]") {
    TEST_ASSERT_TRUE(assert_test_support::assert_program_aborted(
        assert_program(), "construct_overflow"));
}

TEST_CASE("static_string: append past capacity aborts",
          "[static_string][assert]") {
    TEST_ASSERT_TRUE(assert_test_support::assert_program_aborted(
        assert_program(), "append_overflow"));
}

TEST_CASE("static_string: assign past capacity aborts",
          "[static_string][assert]") {
    TEST_ASSERT_TRUE(assert_test_support::assert_program_aborted(
        assert_program(), "assign_overflow"));
}

TEST_CASE("static_string: push_back when full aborts",
          "[static_string][assert]") {
    TEST_ASSERT_TRUE(assert_test_support::assert_program_aborted(
        assert_program(), "push_back_when_full"));
}
