// Shared assert-trigger cases for thermo::espidf_cpp::static_vector.
//
// These triggers intentionally violate thermo::espidf_cpp::static_vector's
// contract so the process aborts. They are compiled into a standalone program
// whose filename contains "_assert" (see test_static_vector_assert.cpp) and are
// also included by the Unity tests, which fork+exec that program and confirm it
// aborted.
#pragma once

#include <array>
#include <span>

#include "assert_test_support.hpp"
#include "espidf_cpp/static_vector.hpp"

namespace static_vector_assert_cases {

using assert_test_support::Case;

// push_back(const T&) past capacity: capacity() == 2, three appends.
inline void push_back_lvalue_when_full() {
    thermo::espidf_cpp::static_vector<int, 2> v;
    v.push_back(1);
    v.push_back(2);
    v.push_back(3);  // Full: aborts.
}

// push_back(T&&) past capacity.
inline void push_back_rvalue_when_full() {
    thermo::espidf_cpp::static_vector<int, 1> v;
    v.push_back(1);
    v.push_back(int{2});  // Full: aborts.
}

// emplace_back past capacity.
inline void emplace_back_when_full() {
    thermo::espidf_cpp::static_vector<int, 1> v;
    v.emplace_back(1);
    v.emplace_back(2);  // Full: aborts.
}

// resize past capacity: assert(count <= N) with count == 3 > N == 2.
inline void resize_past_capacity() {
    thermo::espidf_cpp::static_vector<int, 2> v;
    v.resize(3);
}

inline constexpr std::array<Case, 4> kCases = {{
    {"push_back_lvalue_when_full", &push_back_lvalue_when_full},
    {"push_back_rvalue_when_full", &push_back_rvalue_when_full},
    {"emplace_back_when_full", &emplace_back_when_full},
    {"resize_past_capacity", &resize_past_capacity},
}};

// Read-only view over the case table.
inline constexpr std::span<const Case> cases() noexcept {
    return kCases;
}

}  // namespace static_vector_assert_cases
