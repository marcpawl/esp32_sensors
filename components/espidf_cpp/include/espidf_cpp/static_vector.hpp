// A minimal fixed-capacity sequence container, standing in for C++26's
// `std::static_vector` (not yet implemented in the toolchain's libstdc++).
//
// Storage is inline (no heap): the elements live in a `std::array<T, N>` and
// the container tracks its current size. Elements are value-initialized on
// `push_back`/`emplace_back`/`resize`, covering the parts of the standard
// interface this project needs.
//
// Exceptions are disabled in this firmware (-fno-exceptions); an overflow is a
// hard error: `push_back`/`emplace_back` abort. Callers that must handle
// overflow gracefully use `try_push_back`, which reports failure via its
// return value.
#pragma once

#include <array>
#include <cassert>
#include <cstddef>
#include <cstdlib>
#include <utility>

namespace thermo::espidf_cpp {

// Fixed-capacity vector with `N` slots of inline storage.
//
// Only the subset of the std::static_vector interface used by the firmware is
// provided; it is not a drop-in replacement for the full standard type.
template<typename T, std::size_t N>
class static_vector {
  public:
    using value_type = T;
    using size_type = std::size_t;
    using iterator = T*;
    using const_iterator = const T*;

    static constexpr size_type capacity() noexcept { return N; }

    constexpr static_vector() noexcept = default;

    constexpr size_type size() const noexcept { return size_; }

    constexpr bool empty() const noexcept { return size_ == 0; }

    constexpr bool full() const noexcept { return size_ == N; }

    // Removes every element (does not release storage; there is none to free).
    constexpr void clear() noexcept { size_ = 0; }

    constexpr T& operator[](size_type i) noexcept { return data_[i]; }

    constexpr const T& operator[](size_type i) const noexcept {
        return data_[i];
    }

    constexpr T* data() noexcept { return data_.data(); }

    constexpr const T* data() const noexcept { return data_.data(); }

    constexpr iterator begin() noexcept { return data_.data(); }

    constexpr iterator end() noexcept { return data_.data() + size_; }

    constexpr const_iterator begin() const noexcept { return data_.data(); }

    constexpr const_iterator end() const noexcept {
        return data_.data() + size_;
    }

    constexpr const_iterator cbegin() const noexcept { return data_.data(); }

    constexpr const_iterator cend() const noexcept {
        return data_.data() + size_;
    }

    constexpr T& back() noexcept { return data_[size_ - 1]; }

    constexpr const T& back() const noexcept { return data_[size_ - 1]; }

    // Appends a copy of `value`. Aborts when the vector is full.
    constexpr void push_back(const T& value) {
        if (!try_push_back(value)) {
            abort();
        }
    }

    constexpr void push_back(T&& value) {
        if (!try_push_back(std::move(value))) {
            abort();
        }
    }

    // Appends a value constructed in place. Aborts when the vector is full.
    template<typename... Args>
    constexpr T& emplace_back(Args&&... args) {
        if (full()) {
            abort();
        }
        data_[size_] = T(std::forward<Args>(args)...);
        return data_[size_++];
    }

    // Grows/shrinks the logical size, value-initializing new elements.
    // Precondition: `count` <= capacity() (asserted).
    constexpr void resize(size_type count) {
        assert(count <= N);
        for (size_type i = size_; i < count; ++i) {
            data_[i] = T{};
        }
        size_ = count;
    }

    // Non-aborting append: returns false (and stores nothing) when full.
    constexpr bool try_push_back(const T& value) noexcept {
        if (full()) {
            return false;
        }
        data_[size_++] = value;
        return true;
    }

    constexpr bool try_push_back(T&& value) noexcept {
        if (full()) {
            return false;
        }
        data_[size_++] = std::move(value);
        return true;
    }

  private:
    std::array<T, N> data_{};
    size_type size_ = 0;
};

}  // namespace thermo::espidf_cpp
