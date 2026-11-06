// A fixed-capacity, NUL-terminated string built on thermo::static_vector.
//
// `static_string<N>` stores up to `N` characters plus a terminating NUL. The
// underlying storage is `static_vector<char, N + 1>`, i.e. `N` usable character
// slots and one reserved for the terminator. The buffer always holds a valid
// NUL-terminated string, so `c_str()` is always safe to call.
//
// `size()` and `remaining()` count characters, not buffer slots:
// `size() <= N` and `remaining() == N - size()`, so `remaining()` is exactly
// how many characters can still be appended (0 for a full string).
//
// Exceptions are disabled in this firmware (-fno-exceptions); appending beyond
// capacity is a programming error and asserts, rather than silently truncating.
#pragma once

// NOLINTBEGIN
#pragma GCC diagnostic push  // NOLINT(ignored diagnostics)
#pragma GCC diagnostic pop
// NOLINTEND

#include <cassert>
#include <compare>
#include <cstddef>
#include <cstring>
#include <type_traits>

#include "espidf_cpp/static_vector.hpp"

namespace thermo::espidf_cpp {

namespace detail {

// Length of a C string, evaluated at compile time when `s` is constexpr.
//
// `std::strlen` is not `constexpr` here (this toolchain's libstdc++ does not
// yet provide the C++26 constexpr overloads), so calling it from a `constexpr`
// member would make the whole constructor non-constant. Walking the string
// here keeps `assign`/`append`/the constructor usable in constant expressions.
//
// Precondition: `s` is not null. `append` checks for null *before* calling
// this: a null dereference cannot be handled in a constant expression, which
// would silently demote a constant-evaluated `append(nullptr)` to a runtime
// call.
constexpr std::size_t c_str_len(const char* s) noexcept {
    std::size_t n = 0;
    while (s[n] != '\0') {
        ++n;
    }
    return n;
}

// Orders two NUL-terminated strings by `std::strncmp`, bounded by the *shorter*
// of the two lengths, then by length.
//
// Comparing the common prefix with `strncmp` and letting the lengths break a
// tie gives exact lexicographic (std::string) ordering. A prefix always sorts
// before the longer string, and -- the reason the bound is the *shorter*
// length, not the longer one -- it never needs to look past either terminator.
// Bounding by the longer length instead would be wrong: `strncmp` stops at the
// first NUL, so "a" and "a\0b" (a bounded `append(s, n)` can store an embedded
// NUL) would compare equal even though their lengths differ.
//
// **The constant-evaluation branch is not an optimization.** On this toolchain
// (GCC 13 host, GCC 15 Xtensa) `std::strncmp` is a constexpr *builtin* that
// only accepts objects with static storage duration, so it is usable in a
// constant expression for a literal or a namespace-scope array but not for the
// member buffer of a local `static_string`. Calling it unconditionally would
// make every comparison silently non-constant -- a `static_assert` on one
// would fail to build rather than fall back. So constant evaluation compares
// characters directly, and `strncmp` handles the runtime case.
//
// Precondition: neither argument is null (both callers pass `c_str()`).
constexpr std::strong_ordering compare_c_str(const char* a, std::size_t a_len,
                                             const char* b,
                                             std::size_t b_len) noexcept {
    const std::size_t common = a_len < b_len ? a_len : b_len;

    int prefix = 0;
    if (std::is_constant_evaluated()) {
        std::size_t i = 0;
        while (i < common && a[i] == b[i]) {
            ++i;
        }
        prefix = (i == common) ? 0 : (a[i] < b[i] ? -1 : 1);
    } else {
        prefix = std::strncmp(a, b, common);
    }

    if (prefix < 0) {
        return std::strong_ordering::less;
    }
    if (prefix > 0) {
        return std::strong_ordering::greater;
    }
    // The common prefix matches: the shorter string sorts first.
    if (a_len < b_len) {
        return std::strong_ordering::less;
    }
    if (a_len > b_len) {
        return std::strong_ordering::greater;
    }
    return std::strong_ordering::equal;
}

}  // namespace detail

// Fixed-capacity, always-NUL-terminated string.
//
// `N` is the maximum number of characters (excluding the terminating NUL).
template<std::size_t N>
class static_string {
  public:
    using size_type = std::size_t;

    // Character capacity, excluding the terminating NUL.
    static constexpr size_type capacity() noexcept { return N; }

    // Empty string (buffer holds just the NUL terminator).
    constexpr static_string() noexcept { buf_[0] = '\0'; }

    // Copy from a C string. Asserts if `s` does not fit (len > capacity()).
    constexpr static_string(const char* s) {
        buf_[0] = '\0';  // Guarantee a terminator before any copy.
        assign(s);
    }

    // Number of characters, excluding the terminating NUL.
    constexpr size_type size() const noexcept { return size_; }

    constexpr bool empty() const noexcept { return size_ == 0; }

    // Remaining character capacity (excluding the reserved NUL).
    constexpr size_type remaining() const noexcept {
        return capacity() - size_;
    }

    // Always-valid NUL-terminated view of the contents.
    constexpr const char* c_str() const noexcept { return buf_.data(); }

    // Pointer to the first character (same as c_str()).
    constexpr const char* data() const noexcept { return buf_.data(); }

    constexpr char& operator[](size_type i) noexcept { return buf_[i]; }

    constexpr const char& operator[](size_type i) const noexcept {
        return buf_[i];
    }

    constexpr const char* begin() const noexcept { return buf_.begin(); }

    constexpr const char* end() const noexcept { return buf_.begin() + size(); }

    constexpr const char* cbegin() const noexcept { return begin(); }

    constexpr const char* cend() const noexcept { return end(); }

    // Replaces the contents with `s`. Asserts if `s` does not fit.
    constexpr void assign(const char* s) {
        clear();
        append(s);
    }

    // Appends `s`. Asserts if the result would exceed capacity().
    constexpr void append(const char* s) {
        if (s == nullptr) {
            return;
        }
        append(s, detail::c_str_len(s));
    }

    // Appends the first `n` characters of `s` — a bounded slice of a buffer
    // that need not be NUL-terminated (e.g. a request URI). Asserts if the
    // result would exceed capacity().
    constexpr void append(const char* s, size_type n) {
        if (s == nullptr) {
            return;
        }
        assert(n <= remaining());
        // Overwrite the current terminator, restoring it after each character.
        for (size_type i = 0; i < n; ++i) {
            buf_[size_++] = s[i];
        }
        buf_[size_] = '\0';
    }

    // Appends a single character. Asserts when full.
    constexpr void push_back(char c) {
        assert(remaining() > 0);
        buf_[size_++] = c;
        buf_[size_] = '\0';
    }

    // Non-aborting single-character append: returns false (and stores
    // nothing) when the string is full.
    constexpr bool try_push_back(char c) noexcept {
        if (remaining() == 0) {
            return false;
        }
        buf_[size_++] = c;
        buf_[size_] = '\0';
        return true;
    }

    // Non-aborting append of `n` bytes of `s`: returns false (and stores
    // nothing) when the whole slice does not fit.
    constexpr bool try_append(const char* s, size_type n) noexcept {
        if (s == nullptr || n > remaining()) {
            return false;
        }
        for (size_type i = 0; i < n; ++i) {
            buf_[size_++] = s[i];
        }
        buf_[size_] = '\0';
        return true;
    }

    // Empties the string (buffer still holds the NUL terminator).
    constexpr void clear() noexcept {
        size_ = 0;
        buf_[0] = '\0';
    }

    // --- Comparison ---------------------------------------------------------
    //
    // Content-wise, lexicographic comparison with another `static_string` or a
    // C string, matching `std::string`: the shorter string sorts first when one
    // is a prefix of the other, and an embedded NUL (which the bounded
    // `append(s, n)` can store) is an ordinary character.
    //
    // The capacity is irrelevant: a `static_string<3>` and a
    // `static_string<64>` holding "abc" compare equal to each other and to
    // "abc". The operators are therefore member templates over the other
    // string's capacity rather than taking `const static_string&` (which would
    // only accept the same `N`).
    //
    // Every operator below routes through detail::compare_c_str, so
    // `(a == b)` and `(a <=> b) == 0` always agree.
    //
    // Defining `operator==` and the relational operators explicitly (rather
    // than deriving them from `<=>`) is deliberate: a rewrite would have to
    // pick one of the two `<=>` overloads, so `other < "abc"` (a C string on
    // the right) would not resolve.

    // Three-way comparison with another string, whatever its capacity.
    template<std::size_t M>
    [[nodiscard]] constexpr std::strong_ordering operator<=>(
        const static_string<M>& other) const noexcept {
        return detail::compare_c_str(c_str(), size(), other.c_str(),
                                     other.size());
    }

    // Three-way comparison with a C string. A null pointer is the empty
    // string, matching the other C-string overloads.
    [[nodiscard]] constexpr std::strong_ordering operator<=>(
        const char* s) const noexcept {
        const char* other = (s == nullptr) ? "" : s;
        return detail::compare_c_str(c_str(), size(), other,
                                     detail::c_str_len(other));
    }

    // Equality with another string. Defined rather than generated from `<=>`
    // so the (very common) unequal-length case is rejected before any
    // character is compared.
    template<std::size_t M>
    [[nodiscard]] constexpr bool operator==(
        const static_string<M>& other) const noexcept {
        return size() == other.size() &&
               detail::compare_c_str(c_str(), size(), other.c_str(),
                                     other.size()) ==
                   std::strong_ordering::equal;
    }

    // Equality with a C string. Goes through the same comparison as every
    // other operator, so an embedded NUL is an ordinary character here too and
    // `(a == b)` agrees with `(a <=> b) == 0`.
    [[nodiscard]] constexpr bool operator==(const char* s) const noexcept {
        const char* other = (s == nullptr) ? "" : s;
        return detail::compare_c_str(c_str(), size(), other,
                                     detail::c_str_len(other)) ==
               std::strong_ordering::equal;
    }

    template<std::size_t M>
    [[nodiscard]] constexpr bool operator!=(
        const static_string<M>& other) const noexcept {
        return !(*this == other);
    }

    [[nodiscard]] constexpr bool operator!=(const char* s) const noexcept {
        return !(*this == s);
    }

    // Relational operators against another string. Comparing against a C
    // string also works (`s < "abc"`), through the `const char*` three-way
    // comparison above.
    template<std::size_t M>
    [[nodiscard]] constexpr bool operator<(
        const static_string<M>& other) const noexcept {
        return (*this <=> other) == std::strong_ordering::less;
    }

    template<std::size_t M>
    [[nodiscard]] constexpr bool operator>(
        const static_string<M>& other) const noexcept {
        return (*this <=> other) == std::strong_ordering::greater;
    }

    template<std::size_t M>
    [[nodiscard]] constexpr bool operator<=(
        const static_string<M>& other) const noexcept {
        return (*this <=> other) != std::strong_ordering::greater;
    }

    template<std::size_t M>
    [[nodiscard]] constexpr bool operator>=(
        const static_string<M>& other) const noexcept {
        return (*this <=> other) != std::strong_ordering::less;
    }

  private:
    // N character slots plus one reserved for the terminator.
    static_vector<char, N + 1> buf_;
    // Number of characters stored (the terminator is at buf_[size_]).
    size_type size_ = 0;
};

}  // namespace thermo::espidf_cpp
