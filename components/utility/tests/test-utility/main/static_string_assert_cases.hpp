// Shared assert-trigger cases for thermo::utility::static_string.
//
// These triggers intentionally violate thermo::utility::static_string's
// contract so the process aborts. They are compiled into a standalone program
// whose filename contains "_assert" (see test_static_string_assert.cpp) and are
// also included by the Unity tests, which fork+exec that program and confirm it
// aborted.
#pragma once

// NOLINTBEGIN
#pragma GCC diagnostic push  // NOLINT(ignored diagnostics)
#pragma GCC diagnostic pop
// NOLINTEND

#include <array>
#include <span>
#include <string_view>

#include "assert_test_support.hpp"
#include "utility/static_string.hpp"

namespace static_string_assert_cases {

using assert_test_support::Case;

// Constructing past capacity: strlen("abcdefgh") == 8 > capacity() == 4.
inline void construct_overflow()
{
    thermo::utility::static_string<4> s("abcdefgh");
    (void)s;
}

// Constructing from a view past capacity: size() == 8 > capacity() == 4.
inline void construct_string_view_overflow()
{
    constexpr std::string_view view("abcdefgh");
    thermo::utility::static_string<4> s(view);
    (void)s;
}

// Appending past capacity: remaining() == 2 but strlen("defgh") == 5.
inline void append_overflow()
{
    thermo::utility::static_string<5> s("abc");
    s.append("defgh");
}

// Assigning past capacity: strlen("toolong") == 7 > capacity() == 3.
inline void assign_overflow()
{
    thermo::utility::static_string<3> s("x");
    s.assign("toolong");
}

// push_back with no remaining capacity.
inline void push_back_when_full()
{
    thermo::utility::static_string<2> s;
    s.push_back('a');
    s.push_back('b');
    s.push_back('c');  // Full: remaining() == 0.
}

// back() has no character to return on an empty string.
inline void back_when_empty()
{
    thermo::utility::static_string<4> s;
    (void)s.back();
}

// pop_back() has no character to remove on an empty string.
inline void pop_back_when_empty()
{
    thermo::utility::static_string<4> s;
    s.pop_back();
}

inline constexpr std::array<Case, 7> kCases = {{
    {"construct_overflow", &construct_overflow},
    {"construct_string_view_overflow", &construct_string_view_overflow},
    {"append_overflow", &append_overflow},
    {"assign_overflow", &assign_overflow},
    {"push_back_when_full", &push_back_when_full},
    {"back_when_empty", &back_when_empty},
    {"pop_back_when_empty", &pop_back_when_empty},
}};

// Read-only view over the case table.
inline constexpr std::span<const Case> cases() noexcept
{
    return kCases;
}

}  // namespace static_string_assert_cases
