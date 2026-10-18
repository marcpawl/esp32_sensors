// Single-slot FreeRTOS message queue, typed and RAII-managed.
//
// A C++ wrapper over the FreeRTOS queue API (`xQueueCreate`,
// `xQueueOverwrite`, `xQueueReceive`): the handle stays owned by the object (so
// it is deleted exactly once, including on an early return), the element type
// is the template parameter (so the queue's element size cannot disagree with
// the messages sent), and receiving yields a `std::optional` instead of an
// out-parameter plus a `BaseType_t` result.
//
// The queue has a single slot, so the sender never blocks: a new message
// overwrites any message not yet received.
//
// `send()` and `receive()` use only the parts of the FreeRTOS API that the
// ESP-IDF `linux` target also provides, so this header is host-testable (see
// the `espidf-cpp-test` project).
#pragma once

#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <memory>
#include <optional>
#include <type_traits>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#include "espidf_cpp/handle.hpp"

namespace thermo::espidf_cpp {

// Queue that holds only one message. Sending another message overwrites the
// current one.
template<typename MessageType>
class OneMessageQueue {
  public:
    // FreeRTOS copies messages into and out of the queue storage, so the
    // message type must be copy constructible.
    static_assert(std::is_copy_constructible_v<MessageType>,
                  "OneMessageQueue message type must be copy constructible");
    static_assert(std::is_trivially_copyable_v<MessageType>,
                  "OneMessageQueue message type must be trivially copyable");

    // Creates the single-slot queue. Aborts if FreeRTOS cannot allocate it:
    // without storage the queue could never deliver a message, so there is no
    // useful way to continue.
    OneMessageQueue() {
        QueueHandle_t queue = xQueueCreate(1, sizeof(MessageType));
        if (queue == nullptr) {
            ESP_LOGE(kTag, "xQueueCreate failed");
            abort();
        }
        queue_.reset(queue);
    }

    OneMessageQueue(OneMessageQueue const&) = delete;
    OneMessageQueue(OneMessageQueue&&) = default;
    ~OneMessageQueue() = default;
    OneMessageQueue& operator=(OneMessageQueue const&) = delete;
    OneMessageQueue& operator=(OneMessageQueue&&) = default;

    // Publishes `message`, overwriting any message not yet received. Never
    // blocks (the single slot can always be overwritten).
    void send(MessageType const& message) noexcept {
        xQueueOverwrite(queue_.get(), &message);
    }

    // ISR-safe variant of send(). May be called from an interrupt handler
    // (e.g. a GPIO ISR). Overwrites any unread message, matching send().
    void send_from_isr(MessageType const& message) noexcept {
        BaseType_t higher_priority_task_woken = pdFALSE;
        xQueueOverwriteFromISR(queue_.get(), &message,
                               &higher_priority_task_woken);
        if (higher_priority_task_woken != pdFALSE) {
            portYIELD_FROM_ISR(higher_priority_task_woken);
        }
    }

    // Receives, blocking up to `timeout` ticks. Returns the message when one
    // was dequeued, or `std::nullopt` on timeout.
    std::optional<MessageType> receive(TickType_t timeout) noexcept {
        MessageType out;
        if (xQueueReceive(queue_.get(), &out, timeout) == pdTRUE) {
            return std::optional<MessageType>{out};
        }
        return std::nullopt;
    }

    // Receives, blocking up to `timeout` milliseconds. Convenience overload for
    // callers that prefer a `std::chrono` duration over raw FreeRTOS ticks.
    std::optional<MessageType> receive(
        std::chrono::milliseconds timeout) noexcept {
        auto ticks = pdMS_TO_TICKS(static_cast<std::uint32_t>(timeout.count()));
        return receive(ticks);
    }

    // Raw FreeRTOS handle, for APIs that need one. It remains owned by this
    // object (`vQueueDelete` runs when the queue is destroyed).
    [[nodiscard]] QueueHandle_t handle() const noexcept { return queue_.get(); }

  private:
    static constexpr const char* kTag = "OMQ";

    UniqueQueueHandle queue_;
};

}  // namespace thermo::espidf_cpp
