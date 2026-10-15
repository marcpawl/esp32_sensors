#pragma once

#include <chrono>
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "handle_deleter.hpp"

namespace thermo {
/** Queue that will only old 1 message.  Sending another
 *  message will overwrite any current message.
 */
template<typename MessageType>
class OneMessageQueue {
  public:
    OneMessageQueue() {
        // Single-slot queue: a new send overwrites any unread message.
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

    // Receive, blocking up to `timeout` ticks. Returns the message when one was
    // dequeued, or `std::nullopt` on timeout.
    //
    std::optional<MessageType> receive(TickType_t timeout) noexcept {
        MessageType out;
        if (xQueueReceive(queue_.get(), &out, timeout) == pdTRUE) {
            return std::optional<MessageType>{out};
        }
        return std::nullopt;
    }

    // Receive, blocking up to `timeout` milliseconds. Convenience overload for
    // callers that prefer a `std::chrono` duration over raw FreeRTOS ticks.
    std::optional<MessageType> receive(
        std::chrono::milliseconds timeout) noexcept {
        auto ticks = pdMS_TO_TICKS(static_cast<std::uint32_t>(timeout.count()));
        return receive(ticks);
    }

  private:
    char const* const kTag = "OMQ";
    UniqueQueueHandle queue_;
};
}  // namespace thermo
