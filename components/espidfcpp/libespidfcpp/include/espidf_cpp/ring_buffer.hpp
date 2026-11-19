// Typed, RAII-managed FreeRTOS ring buffer.
//
// A C++ wrapper over the ESP-IDF ring-buffer API (`xRingbufferCreateNoSplit`,
// `xRingbufferSend`, `xRingbufferReceive`, ...). The element type is the
// template parameter, so the buffer only accepts and only returns that type,
// and the item size handed to FreeRTOS cannot disagree with the messages
// passed in. The handle is owned by the object (so it is deleted exactly
// once), and receiving yields a `std::optional` instead of an out-parameter
// plus a `BaseType_t` result.
//
// The buffer is always created as a no-split buffer: an item is stored in one
// contiguous run of memory and is never split across the end of the buffer.
// Split buffers (and byte buffers, which do not keep item boundaries) are not
// wrapped.
//
// Unlike the single-slot `OneMessageQueue`, a ring buffer holds a configurable
// number of items, so `send()` can fail when the buffer is full.
//
// Task-context calls block on `TickType_t` (or `std::chrono`) waits. The
// `...FromIsr()` variants are for interrupt handlers only and are named after
// the ESP-IDF convention; no ring-buffer entry point is valid in both
// contexts, so none of these methods carries `[[thermo::isr_safe]]`.
#pragma once

// NOLINTBEGIN
#pragma GCC diagnostic push  // NOLINT(ignored diagnostics)
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/ringbuf.h"
#pragma GCC diagnostic pop
// NOLINTEND

#include <array>
#include <bit>
#include <chrono>
#include <optional>
#include <type_traits>

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>

#include "espidf_cpp/handle.hpp"

namespace thermo::espidf_cpp {

// Ring buffer holding up to `capacity` items of `ElementType`.
template<typename ElementType>
class RingBuffer {
  public:
    // FreeRTOS copies items into and out of the buffer storage, so the item
    // type must be trivially copyable.
    static_assert(std::is_trivially_copyable_v<ElementType>,
                  "RingBuffer element type must be trivially copyable");

    // Creates a no-split buffer sized for exactly `capacity` items. FreeRTOS
    // cannot create a no-split buffer for fewer than two items (it always
    // reserves room for two item headers), so `capacity` must be at least 2.
    // Aborts if the capacity is too small or FreeRTOS cannot allocate it:
    // without storage the buffer could never deliver an item, so there is no
    // useful way to continue.
    explicit RingBuffer(std::size_t capacity) {
        if (capacity < kMinimumCapacity) {
            ESP_LOGE(kTag, "ring buffer capacity must be at least %u",
                     static_cast<unsigned>(kMinimumCapacity));
            abort();
        }
        RingbufHandle_t buffer =
            xRingbufferCreateNoSplit(sizeof(ElementType), capacity);
        if (buffer == nullptr) {
            ESP_LOGE(kTag, "xRingbufferCreateNoSplit failed");
            abort();
        }
        buffer_.reset(buffer);
    }

    RingBuffer(RingBuffer const&) = delete;
    RingBuffer(RingBuffer&&) = default;
    ~RingBuffer() = default;
    RingBuffer& operator=(RingBuffer const&) = delete;
    RingBuffer& operator=(RingBuffer&&) = default;

    // Sends `item`, blocking up to `timeout` ticks for space. Returns true when
    // the item was stored, false on timeout or when the buffer cannot hold an
    // item of this size (which cannot happen for a buffer created for exactly
    // this element type).
    bool send(ElementType const& item, TickType_t timeout) noexcept {
        return xRingbufferSend(buffer_.get(), &item, sizeof(ElementType),
                               timeout) == pdTRUE;
    }

    // Convenience overload for callers that prefer a `std::chrono` duration
    // over raw FreeRTOS ticks.
    bool send(ElementType const& item,
              std::chrono::milliseconds timeout) noexcept {
        return send(item, to_ticks(timeout));
    }

    // ISR-safe variant of send(). May only be called from an interrupt
    // handler. Returns true when the item was stored, false when the buffer is
    // full (an ISR never blocks).
    bool sendFromIsr(ElementType const& item) noexcept {
        BaseType_t higher_priority_task_woken = pdFALSE;
        const BaseType_t sent =
            xRingbufferSendFromISR(buffer_.get(), &item, sizeof(ElementType),
                                   &higher_priority_task_woken);
        if (higher_priority_task_woken != pdFALSE) {
            portYIELD_FROM_ISR(higher_priority_task_woken);
        }
        return sent == pdTRUE;
    }

    // Receives, blocking up to `timeout` ticks. Returns the item, or
    // `std::nullopt` on timeout.
    std::optional<ElementType> receive(TickType_t timeout) noexcept {
        size_t size = 0;
        void* storage = xRingbufferReceive(buffer_.get(), &size, timeout);
        if (storage == nullptr) {
            return std::nullopt;
        }
        const ElementType item = copy_item(storage, size);
        vRingbufferReturnItem(buffer_.get(), storage);
        return item;
    }

    // Receives, blocking up to `timeout` milliseconds.
    std::optional<ElementType> receive(
        std::chrono::milliseconds timeout) noexcept {
        return receive(to_ticks(timeout));
    }

    // ISR-safe variant of receive(). May only be called from an interrupt
    // handler. Returns the item, or `std::nullopt` when the buffer is empty.
    std::optional<ElementType> receiveFromIsr() noexcept {
        size_t size = 0;
        void* storage = xRingbufferReceiveFromISR(buffer_.get(), &size);
        if (storage == nullptr) {
            return std::nullopt;
        }
        const ElementType item = copy_item(storage, size);
        BaseType_t higher_priority_task_woken = pdFALSE;
        vRingbufferReturnItemFromISR(buffer_.get(), storage,
                                     &higher_priority_task_woken);
        if (higher_priority_task_woken != pdFALSE) {
            portYIELD_FROM_ISR(higher_priority_task_woken);
        }
        return item;
    }

    // Raw FreeRTOS handle, for APIs that need one. It remains owned by this
    // object (`vRingbufferDelete` runs when the buffer is destroyed).
    [[nodiscard]] RingbufHandle_t handle() const noexcept {
        return buffer_.get();
    }

  private:
    static constexpr const char* kTag = "RINGBUF";

    // A no-split buffer always reserves room for two item headers, so the
    // smallest one FreeRTOS accepts holds two items.
    static constexpr std::size_t kMinimumCapacity = 2;

    static TickType_t to_ticks(std::chrono::milliseconds timeout) noexcept {
        return pdMS_TO_TICKS(static_cast<std::uint32_t>(timeout.count()));
    }

    // Copies an item out of the ring buffer's storage. FreeRTOS stores items
    // only 32-bit aligned, which may be less strict than the element type, so
    // the bytes are copied into an aligned array and reinterpreted with
    // `std::bit_cast` rather than loaded through a typed pointer.
    static ElementType copy_item(void* storage, size_t size) noexcept {
        // A no-split buffer returns an item exactly as it was sent.
        assert(size == sizeof(ElementType) && "ring buffer item size mismatch");
        (void)size;
        std::array<std::byte, sizeof(ElementType)> bytes{};
        std::memcpy(bytes.data(), storage, sizeof(ElementType));
        return std::bit_cast<ElementType>(bytes);
    }

    UniqueRingbufHandle buffer_;
};

}  // namespace thermo::espidf_cpp
