// Tests for the project-local fixed-capacity container
// (thermo::utility::static_vector).
//
// This stands in for C++26 std::static_vector, which the toolchain's libstdc++
// does not yet provide. It is pure logic with no IDF dependencies, so it is
// covered on the host. Overflow of push_back/emplace_back is a hard error
// (aborts) and is verified in forked children that exec the standalone
// `static_vector_assert` program (see the [assert] cases at the end);
// try_push_back reports overflow via a return value and is tested inline.
#include <cstddef>
#include <cstdint>

#include "assert_unity_support.hpp"
#include "unity.h"
#include "utility/static_string.hpp"
#include "utility/static_vector.hpp"

using thermo::utility::static_string;
using thermo::utility::static_vector;

// --- constexpr usability ----------------------------------------------------
//
// These evaluate the container at compile time, so they fail to build if any
// of the exercised operations stops being constexpr.
namespace {

constexpr bool static_vector_constexpr_builds() {
    static_vector<int, 4> v;
    v.push_back(1);
    v.push_back(2);
    const bool used = v.size() == 2 && v[0] == 1 && v.back() == 2 &&
                      !v.full() && !v.empty() && v.data()[1] == 2 &&
                      *v.begin() == 1 && *(v.end() - 1) == 2;
    v.clear();
    const bool cleared = v.empty() && v.size() == 0;
    v.resize(3);
    const bool resized = v.size() == 3 && v[1] == 0;

    static_vector<int, 2> full_v;
    const bool first = full_v.try_push_back(7);
    const bool second = full_v.try_push_back(8);
    const bool rejected = !full_v.try_push_back(9);  // Now full.
    return used && cleared && resized && first && second && rejected;
}

}  // namespace

static_assert(static_vector_constexpr_builds(),
              "static_vector must be usable in constant expressions");

// --- Capacity and size ------------------------------------------------------

TEST_CASE("static_vector: default is empty with full capacity",
          "[static_vector]") {
    static_vector<int, 4> v;
    TEST_ASSERT_EQUAL_UINT(0, v.size());
    TEST_ASSERT_TRUE(v.empty());
    TEST_ASSERT_FALSE(v.full());
    TEST_ASSERT_EQUAL_UINT(4, v.capacity());
}

TEST_CASE("static_vector: constructs from an iterator range",
          "[static_vector]") {
    const int values[] = {7, 8};
    const static_vector<int, 3> v(values, values + 2);
    TEST_ASSERT_EQUAL_UINT(2, v.size());
    TEST_ASSERT_EQUAL_INT(7, v[0]);
    TEST_ASSERT_EQUAL_INT(8, v[1]);
}

TEST_CASE("static_vector: push_back grows size and reaches full",
          "[static_vector]") {
    static_vector<int, 3> v;
    v.push_back(10);
    v.push_back(20);
    v.push_back(30);

    TEST_ASSERT_EQUAL_UINT(3, v.size());
    TEST_ASSERT_TRUE(v.full());
    TEST_ASSERT_FALSE(v.empty());
    TEST_ASSERT_EQUAL_INT(10, v[0]);
    TEST_ASSERT_EQUAL_INT(20, v[1]);
    TEST_ASSERT_EQUAL_INT(30, v[2]);
    TEST_ASSERT_EQUAL_INT(30, v.back());
}

TEST_CASE("static_vector: clear empties without changing capacity",
          "[static_vector]") {
    static_vector<int, 3> v;
    v.push_back(1);
    v.push_back(2);
    v.clear();

    TEST_ASSERT_EQUAL_UINT(0, v.size());
    TEST_ASSERT_TRUE(v.empty());
    TEST_ASSERT_FALSE(v.full());
    TEST_ASSERT_EQUAL_UINT(3, v.capacity());
}

// --- try_push_back reports overflow instead of aborting ----------------------

TEST_CASE("static_vector: try_push_back succeeds until full then fails",
          "[static_vector]") {
    static_vector<int, 2> v;
    TEST_ASSERT_TRUE(v.try_push_back(1));
    TEST_ASSERT_TRUE(v.try_push_back(2));
    TEST_ASSERT_FALSE(v.try_push_back(3));  // Full: rejected, not stored.

    TEST_ASSERT_EQUAL_UINT(2, v.size());
    TEST_ASSERT_EQUAL_INT(2, v[1]);  // The rejected value did not overwrite.
}

TEST_CASE("static_vector: try_push_back accepts rvalues", "[static_vector]") {
    static_vector<static_string<8>, 2> v;
    TEST_ASSERT_TRUE(v.try_push_back(static_string<8>("hello")));
    TEST_ASSERT_TRUE(v.try_push_back(static_string<8>("world")));
    TEST_ASSERT_FALSE(v.try_push_back(static_string<8>("nope")));

    TEST_ASSERT_EQUAL_UINT(2, v.size());
    TEST_ASSERT_EQUAL_STRING("hello", v[0].c_str());
    TEST_ASSERT_EQUAL_STRING("world", v[1].c_str());
}

// --- emplace_back constructs in place ---------------------------------------

TEST_CASE("static_vector: emplace_back constructs in place",
          "[static_vector]") {
    static_vector<static_string<8>, 3> v;
    v.emplace_back("first");
    v.emplace_back("xxxxx");

    TEST_ASSERT_EQUAL_UINT(2, v.size());
    TEST_ASSERT_EQUAL_STRING("first", v[0].c_str());
    TEST_ASSERT_EQUAL_STRING("xxxxx", v[1].c_str());
}

// --- resize ----------------------------------------------------------------

TEST_CASE("static_vector: resize grows with value-initialized elements",
          "[static_vector]") {
    static_vector<int, 4> v;
    v.push_back(7);
    v.resize(3);

    TEST_ASSERT_EQUAL_UINT(3, v.size());
    TEST_ASSERT_EQUAL_INT(7, v[0]);
    TEST_ASSERT_EQUAL_INT(0, v[1]);  // Value-initialized.
    TEST_ASSERT_EQUAL_INT(0, v[2]);
}

TEST_CASE("static_vector: resize shrinks", "[static_vector]") {
    static_vector<int, 4> v;
    v.push_back(1);
    v.push_back(2);
    v.push_back(3);
    v.resize(1);

    TEST_ASSERT_EQUAL_UINT(1, v.size());
    TEST_ASSERT_EQUAL_INT(1, v[0]);
}

TEST_CASE("static_vector: resize to capacity is allowed", "[static_vector]") {
    static_vector<int, 2> v;
    v.resize(2);
    TEST_ASSERT_TRUE(v.full());
    TEST_ASSERT_EQUAL_UINT(2, v.size());
}

// --- data / iteration -------------------------------------------------------

TEST_CASE("static_vector: data and iterators reflect the live elements",
          "[static_vector]") {
    static_vector<int, 4> v;
    v.push_back(1);
    v.push_back(2);
    v.push_back(3);

    int sum = 0;
    for (int x : v) {
        sum += x;
    }
    TEST_ASSERT_EQUAL_INT(6, sum);

    // data() exposes the inline storage; contiguous like std::vector.
    TEST_ASSERT_EQUAL_INT(1, v.data()[0]);
    TEST_ASSERT_EQUAL_INT(3, v.data()[2]);
    TEST_ASSERT_EQUAL_PTR(v.begin(), v.data());
    TEST_ASSERT_EQUAL_PTR(v.end(), v.data() + v.size());
}

TEST_CASE("static_vector: cbegin and cend iterate const elements",
          "[static_vector]") {
    static_vector<int, 4> v;
    v.push_back(4);
    v.push_back(5);
    v.push_back(6);

    const auto& cv = v;
    int sum = 0;
    for (auto it = cv.cbegin(); it != cv.cend(); ++it) {
        sum += *it;
    }
    TEST_ASSERT_EQUAL_INT(15, sum);

    TEST_ASSERT_EQUAL_PTR(v.cbegin(), v.data());
    TEST_ASSERT_EQUAL_PTR(v.cend(), v.data() + v.size());
}

// --- Non-null data even when empty ------------------------------------------

TEST_CASE("static_vector: empty data pointer is valid", "[static_vector]") {
    static_vector<int, 4> v;
    TEST_ASSERT_NOT_NULL(v.data());
    TEST_ASSERT_EQUAL_PTR(v.begin(), v.end());
}

// --- Overflow asserts (fork + exec the standalone program) ------------------

namespace {

// Path to the sibling static_vector_assert program (shared by the cases below).
const assert_test_support::AssertProgramPath& assert_program() {
    static const auto path =
        assert_test_support::assert_program_path("static_vector_assert");
    return path;
}

}  // namespace

TEST_CASE("static_vector: push_back(lvalue) when full aborts",
          "[static_vector][assert]") {
    TEST_ASSERT_TRUE(assert_test_support::assert_program_aborted(
        assert_program(), "push_back_lvalue_when_full"));
}

TEST_CASE("static_vector: push_back(rvalue) when full aborts",
          "[static_vector][assert]") {
    TEST_ASSERT_TRUE(assert_test_support::assert_program_aborted(
        assert_program(), "push_back_rvalue_when_full"));
}

TEST_CASE("static_vector: emplace_back when full aborts",
          "[static_vector][assert]") {
    TEST_ASSERT_TRUE(assert_test_support::assert_program_aborted(
        assert_program(), "emplace_back_when_full"));
}

TEST_CASE("static_vector: resize past capacity aborts",
          "[static_vector][assert]") {
    TEST_ASSERT_TRUE(assert_test_support::assert_program_aborted(
        assert_program(), "resize_past_capacity"));
}
