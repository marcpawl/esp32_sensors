

#pragma once

#include <cassert>
#include <concepts>
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
    template<size_t max_length>
    thermo::utility::static_string<max_length> read_str(
        char const* key, std::string_view const& default_value) {
        // nvs_get_str reports the length including the NUL terminator.
        std::size_t len = 0;
        esp_err_t error = nvs_get_str(handle_, key, nullptr, &len);
        if (error == ESP_ERR_NVS_NOT_FOUND) {
            thermo::utility::static_string<max_length> out;
            out.append(default_value.data(), default_value.size());
            return out;
        }
        ESP_ERROR_CHECK(error);
        assert(len > 0);
        assert(len <= max_length + 1);

        char buffer[max_length + 1] = {};
        error = nvs_get_str(handle_, key, buffer, &len);
        ESP_ERROR_CHECK(error);
        return thermo::utility::static_string<max_length>{buffer};
    }

    // Retrieve a number from the NVS storage.
    // @param key Name of the key that is stored.
    // @param default_value Value to use if there is no entry with the key.
    // TODO(AI!) constrain T to an integer
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
        static_assert(sizeof(float) == sizeof(std::uint32_t),
                      "32 bit floats expected");
        // NVS has no float type; store as the raw bit pattern via a blob.
        float v;
        std::size_t len = sizeof(v);
        esp_err_t error = nvs_get_blob(handle_, key, &v, &len);
        if (error == ESP_ERR_NVS_NOT_FOUND) {
            return default_value;
        }
        ESP_ERROR_CHECK(error);
        assert(len == sizeof(float));
        return v;
    }

    template<size_t max_length>
    void write(char const* key,
               thermo::utility::static_string<max_length> const& value)
        requires(mode == NvsMode::ReadWrite)
    {
        esp_err_t error = nvs_set_str(handle_, key, value.data());
        ESP_ERROR_CHECK(error);
    }

    // TODO(AI!) constrain value to integer
    void write(char const* key, auto value)
        requires(mode == NvsMode::ReadWrite)
    {
        esp_err_t error;
        if constexpr (std::is_same_v<decltype(value), std::uint8_t>) {
            error = nvs_set_u8(handle_, key, value);
        } else if constexpr (std::is_same_v<decltype(value), std::uint16_t>) {
            error = nvs_set_u16(handle_, key, value);
        } else if constexpr (std::is_same_v<decltype(value), std::uint32_t>) {
            error = nvs_set_u32(handle_, key, value);
        } else if constexpr (std::is_same_v<decltype(value), std::int8_t>) {
            error = nvs_set_i8(handle_, key, value);
        } else if constexpr (std::is_same_v<decltype(value), std::int16_t>) {
            error = nvs_set_i16(handle_, key, value);
        } else if constexpr (std::is_same_v<decltype(value), std::int32_t>) {
            error = nvs_set_i32(handle_, key, value);
        } else {
            static_assert(std::is_same_v<decltype(value), void>,
                          "Unexpected numeric type");
        }
        ESP_ERROR_CHECK(error);
    }

    void write(char const* key, float value)
        requires(mode == NvsMode::ReadWrite)
    {
        static_assert(sizeof(float) == sizeof(std::uint32_t),
                      "32 bit floats expected");
        // Store the float as a blob
        esp_err_t error = nvs_set_blob(handle_, key, &value, sizeof(value));
        ESP_ERROR_CHECK(error);
    }

  private:
    nvs_handle_t handle_;
};
}  // namespace thermo::espidf_cpp
