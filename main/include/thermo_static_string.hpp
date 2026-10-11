// A fixed-capacity, NUL-terminated string built on thermo::static_vector.
//
// `static_string<N>` stores up to `N` characters plus a terminating NUL. The
// underlying storage is `static_vector<char, N + 1>`, i.e. `N` usable character
// slots and one reserved for the terminator. The buffer always holds a valid
// NUL-terminated string, so `c_str()` is always safe to call.
//
// Exceptions are disabled in this firmware (-fno-exceptions); appending beyond
// capacity is a programming error and asserts, rather than silently truncating.
#pragma once

#include <cassert>
#include <cstddef>
#include <cstring>

#include "thermo_static_vector.hpp"

namespace thermo {

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
    static_string() noexcept { buf_.push_back('\0'); }

    // Copy from a C string. Asserts if `s` does not fit (len > capacity()).
    static_string(const char* s) {
        buf_.push_back('\0');  // Guarantee a terminator before any copy.
        assign(s);
    }

    // Number of characters, excluding the terminating NUL.
    size_type size() const noexcept {
        return buf_.size() == 0 ? 0 : buf_.size() - 1;
    }

    bool empty() const noexcept { return size() == 0; }

    // Remaining character capacity (excluding the reserved NUL).
    size_type remaining() const noexcept { return capacity() - size(); }

    // Always-valid NUL-terminated view of the contents.
    const char* c_str() const noexcept { return buf_.data(); }

    // Pointer to the first character (same as c_str()).
    const char* data() const noexcept { return buf_.data(); }

    char& operator[](size_type i) noexcept { return buf_[i]; }

    const char& operator[](size_type i) const noexcept { return buf_[i]; }

    const char* begin() const noexcept { return buf_.begin(); }

    const char* end() const noexcept { return buf_.begin() + size(); }

    const char* cbegin() const noexcept { return begin(); }

    const char* cend() const noexcept { return end(); }

    // Replaces the contents with `s`. Asserts if `s` does not fit.
    void assign(const char* s) {
        clear();
        append(s);
    }

    // Appends `s`. Asserts if the result would exceed capacity().
    void append(const char* s) {
        if (s == nullptr) {
            return;
        }
        const size_type n = std::strlen(s);
        assert(n <= remaining());
        // Overwrite the current terminator, restoring it after each character.
        for (size_type i = 0; i < n; ++i) {
            buf_.back() = s[i];
            buf_.push_back('\0');
        }
    }

    // Appends a single character. Asserts when full.
    void push_back(char c) {
        assert(remaining() > 0);
        buf_.back() = c;
        buf_.push_back('\0');
    }

    // Empties the string (buffer still holds the NUL terminator).
    void clear() noexcept {
        buf_.clear();
        buf_.push_back('\0');
    }

  private:
    // N character slots plus one reserved for the terminator.
    static_vector<char, N + 1> buf_;
};

}  // namespace thermo
