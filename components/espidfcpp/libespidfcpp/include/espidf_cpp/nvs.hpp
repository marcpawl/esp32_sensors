

#pragma once

#include <array>
#include <bit>
#include <cassert>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <string_view>

// NOLINTBEGIN
#pragma GCC diagnostic push  // NOLINT(ignored diagnostics)
#include "nvs.h"
#include "nvs_flash.h"
#pragma GCC diagnostic pop
// NOLINTEND

#include "utility/static_string.hpp"

namespace thermo::espidf_cpp {

enum class NvsMode : std::uint8_t {
    Read = NVS_READONLY,
    ReadWrite = NVS_READWRITE
};

template<NvsMode mode>
class Nvs {
  public:
    // @param storge_namespace Location in NVS where the data is stored.
    Nvs(char const* storage_namespace) {
        ESP_ERROR_CHECK(nvs_open(storage_namespace,
                                 static_cast<nvs_open_mode_t>(mode), &handle_));
    }

    Nvs(const Nvs&) = delete;
    Nvs(Nvs&&) = delete;

    ~Nvs() { nvs_close(handle_); }

    Nvs& operator=(const Nvs&) = delete;
    Nvs& operator=(Nvs&&) = delete;

    // Retrieve a string from the NVS storage.
    // @param key Name of the key that is stored.
    // @param default_value Value to use if there is no entry with the key.
    template<std::size_t max_length>
    thermo::utility::static_string<max_length> read(
        char const* key, std::string_view const& default_value) {
        assert(default_value.size() <= max_length);
        // nvs_get_str reports the length including the NUL terminator.
        std::size_t len = 0;
        esp_err_t error = nvs_get_str(handle_, key, nullptr, &len);
        if (error == ESP_ERR_NVS_NOT_FOUND) {
            return thermo::utility::static_string<max_length>{default_value};
        }
        ESP_ERROR_CHECK(error);
        assert(len > 0);
        assert(len <= max_length + 1);

        std::array<char, max_length + 1> buffer{};
        error = nvs_get_str(handle_, key, buffer.data(), &len);
        ESP_ERROR_CHECK(error);
        assert(len > 0);
        assert(len <= buffer.size());
        return thermo::utility::static_string<max_length>{
            std::string_view(buffer.data(), len - 1)};
    }

    // Retrieve a number from the NVS storage.
    // @param key Name of the key that is stored.
    // @param default_value Value to use if there is no entry with the key.
    template<std::integral T>
    T read(const char* key, T default_value) {
        T v{};
        esp_err_t error;
        if constexpr (std::is_same_v<T, std::uint8_t>) {
            error = nvs_get_u8(handle_, key, &v);
        } else if constexpr (std::is_same_v<T, std::uint16_t>) {
            error = nvs_get_u16(handle_, key, &v);
        } else if constexpr (std::is_same_v<T, std::uint32_t>) {
            error = nvs_get_u32(handle_, key, &v);
        } else if constexpr (std::is_same_v<T, std::int8_t>) {
            error = nvs_get_i8(handle_, key, &v);
        } else if constexpr (std::is_same_v<T, std::int16_t>) {
            error = nvs_get_i16(handle_, key, &v);
        } else if constexpr (std::is_same_v<T, std::int32_t>) {
            error = nvs_get_i32(handle_, key, &v);
        } else {
            static_assert(std::is_same_v<T, void>, "Unexpected numeric type");
        }
        if (error == ESP_ERR_NVS_NOT_FOUND) {
            return default_value;
        }
        ESP_ERROR_CHECK(error);
        return v;
    }

    // Retrieve a number from the NVS storage.
    // @param key Name of the key that is stored.
    // @param default_value Value to use if there is no entry with the key.
    float read(const char* key, float default_value) {
        // std::start_lifetime_as is not available in either pinned container
        // toolchain (linux target GCC 13 with gnu++2b, Xtensa GCC 15), so
        // bit_cast gives the same object-representation result portably.
        alignas(float) std::array<std::byte, sizeof(float)> buffer{};
        std::size_t len = buffer.size();
        esp_err_t error = nvs_get_blob(handle_, key, buffer.data(), &len);
        if (error == ESP_ERR_NVS_NOT_FOUND) {
            return default_value;
        }
        ESP_ERROR_CHECK(error);
        assert(len == buffer.size());
        return std::bit_cast<float>(buffer);
    }

    template<std::size_t max_length>
    void write(char const* key,
               thermo::utility::static_string<max_length> const& value)
        requires(mode == NvsMode::ReadWrite)
    {
        esp_err_t error = nvs_set_str(handle_, key, value.data());
        ESP_ERROR_CHECK(error);
    }

    template<std::integral T>
    void write(char const* key, T value)
        requires(mode == NvsMode::ReadWrite)
    {
        esp_err_t error;
        if constexpr (std::is_same_v<T, std::uint8_t>) {
            error = nvs_set_u8(handle_, key, value);
        } else if constexpr (std::is_same_v<T, std::uint16_t>) {
            error = nvs_set_u16(handle_, key, value);
        } else if constexpr (std::is_same_v<T, std::uint32_t>) {
            error = nvs_set_u32(handle_, key, value);
        } else if constexpr (std::is_same_v<T, std::int8_t>) {
            error = nvs_set_i8(handle_, key, value);
        } else if constexpr (std::is_same_v<T, std::int16_t>) {
            error = nvs_set_i16(handle_, key, value);
        } else if constexpr (std::is_same_v<T, std::int32_t>) {
            error = nvs_set_i32(handle_, key, value);
        } else {
            static_assert(std::is_same_v<T, void>, "Unexpected numeric type");
        }
        ESP_ERROR_CHECK(error);
    }

    void write(char const* key, float value)
        requires(mode == NvsMode::ReadWrite)
    {
        // Store the float as a blob.
        esp_err_t error = nvs_set_blob(handle_, key, &value, sizeof(value));
        ESP_ERROR_CHECK(error);
    }

  private:
    nvs_handle_t handle_;
};
}  // namespace thermo::espidf_cpp
