#pragma once

#include <cassert>
#include <functional>

#include "../../../../espidfcpp/libespidfcpp/include/espidf_cpp/handle.hpp"
#include "boost/sml.hpp"

#include "espidf_cpp/timer.hpp"
#include "hal/switch_types.hpp"
#include "logic/reboot.hpp"

namespace thermo {

struct ToggleSwitchSm {
    // 1. Context storing Switch reference, last reading, and callbacks
    struct Context {
        hal::SwitchState last_reading_;
        std::function<[[thermo::isr_safe]] void(hal::SwitchState)> const
            stable_callback_;
        std::function<[[thermo::isr_safe]] void()> const bouncing_callback_;
        std::function<[[thermo::isr_safe]] hal::SwitchState()> const
            read_switch_;

        // @param stable_cb Callback used to indicate that the
        //   switch has not changed in a while.
        // @param bouncing_cm Callback used to indicate that the
        //   switch is rapidly changing.
        // @param read_switch Function to call to get the current
        //   switch state.
        Context(
            std::function<[[thermo::isr_safe]] void(hal::SwitchState)>
                stable_cb,
            std::function<[[thermo::isr_safe]] void()> bouncing_cb,
            std::function<[[thermo::isr_safe]] hal::SwitchState()> read_switch)
            : stable_callback_(std::move(stable_cb)),
              bouncing_callback_(std::move(bouncing_cb)),
              read_switch_(read_switch) {}
    };

    // 2. Events
    struct SwitchChangedEvent {
        hal::SwitchState state_;
    };

    struct DebouncedEvent {
        hal::SwitchState state_;
    };

    struct UnstableEvent {};

    struct DebounceTimerExpiredEvent {};

    struct UnstableTimerExpiredEvent {
        hal::SwitchState state_;
    };

    // 3. States
    struct initializing {
        auto on_entry() {
            return [](Context& ctx) { ctx.last_reading_ = ctx.read_switch_(); };
        }
    };

    // Composite State 'debouncing' containing timers, constructor, nested
    // sub-states, on_entry, and on_exit
    struct debouncing {
        thermo::espidf_cpp::Timer debouncing_timer_;
#if 0
        thermo::espidf_cpp::Timer unstable_timer_;
#endif

        // Constructor for debouncing composite state
        debouncing() : debouncing_timer_([this]() -> void {}) {}

        // Nested sub-state 'entry'
        struct entry {
            [[thermo::isr_safe]] auto on_entry() {
                return [](debouncing& parent, auto sm) {
#if 0
                    auto send_debouncing_timer_expired = [sm]() mutable {
                        sm.process_event(DebounceTimerExpiredEvent{});
                    };
                    parent.debouncing_timer_.set_callback(send_debouncing_timer_expired);

                    auto send_unstable_timer_expired = [sm]() mutable {
                        sm.process_event(UnstableTimerExpiredEvent{false});
                    };
                    parent.unstable_timer_.set_callback(send_unstable_timer_expired);

                    parent.debouncing_timer_.start();
                    parent.unstable_timer_.start();
#endif
                };
            }
        };

        // Nested sub-state 'changed' with on_entry calling
        // debouncing_timer_.restart()
        struct changed {
#if 0
            [[thermo::isr_safe]] auto on_entry() {
                return [](debouncing& parent) {
                    parent.debouncing_timer_.restart();
                };
            }

            [[thermo::isr_safe]] auto on_exit() {
                return []() {
                };
            }
#endif
        };

        [[thermo::isr_safe]] auto on_entry() {
            return [](Context& ctx, debouncing& self) {
                if (ctx.bouncing_callback_) {
                    ctx.bouncing_callback_();
                }
            };
        }

        [[thermo::isr_safe]] auto on_exit() {
            return [](debouncing& self) {
#if 0
                self.debouncing_timer_.cancel();
                self.unstable_timer_.cancel();
#endif
            };
        }

        auto operator()() const {
            using namespace boost::sml;

            return make_transition_table(
                // Immediate / anonymous transition from entry to changed
                *state<entry> = state<changed>,

                // Self-transition on changed state: exit and re-enter
                // state<changed> on SwitchChangedEvent
                state<changed> + event<SwitchChangedEvent> /
                                     [](const SwitchChangedEvent& event,
                                        Context& ctx, debouncing& self) {
                                         ctx.last_reading_ = event.state_;
                                     } = state<changed>);
        }
    };

    struct stable {
        [[thermo::isr_safe]] auto on_entry() {
            return [](Context& ctx) {
                if (ctx.stable_callback_) {
                    ctx.stable_callback_(ctx.last_reading_);
                }
            };
        }
    };

    struct unstable {
        [[thermo::isr_safe]] auto on_entry() {
            return []() { reboot(); };
        }
    };

    // 4. Transition Table
    auto operator()() const {
        using namespace boost::sml;

        return make_transition_table(
            *state<initializing> = state<debouncing>,

            state<debouncing> +
                event<DebouncedEvent> /
                    [](const DebouncedEvent& event, Context& ctx) {
                        ctx.last_reading_ = event.state_;
                    } = state<stable>,

            state<debouncing> + event<UnstableEvent> = state<unstable>);
    }
};

#if 0
int main() {
    Switch my_switch;
    my_switch.current_hardware_state = true;
    auto on_stable = [](bool state) {
        std::cout << "[Callback] Switch is stable with state: " << (state ? "true" : "false") << "\n";
    };
    auto on_bouncing = []() {
        std::cout << "[Callback] Bouncing state callback triggered!\n";
    };

    ToggleSwitchSm::Context ctx{my_switch, on_stable, on_bouncing};
    boost::smlsm<ToggleSwitchSm, boost::smldependency<ToggleSwitchSm::Context&>> sm{ctx};

    assert(sm.is(boost::smlstate<ToggleSwitchSm::debouncing>));
    assert(sm.is(boost::smlstate<ToggleSwitchSm::debouncing::changed>));

    std::cout << "ToggleSwitchSm initialized successfully.\n";

    return 0;
}
#endif

}  // namespace thermo
