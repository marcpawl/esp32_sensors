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
#include <cstddef>

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

  private:
    // N character slots plus one reserved for the terminator.
    static_vector<char, N + 1> buf_;
    // Number of characters stored (the terminator is at buf_[size_]).
    size_type size_ = 0;
};

}  // namespace thermo::espidf_cpp
