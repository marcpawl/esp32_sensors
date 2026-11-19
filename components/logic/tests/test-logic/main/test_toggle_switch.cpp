
#include "unity.h"
#include "boost/sml.hpp"

#include "hal/switch_types.hpp"
#include "logic/toggle_switch.hpp"

namespace hal = thermo::hal;
using thermo::ToggleSwitchSm;

TEST_CASE("TODO", "[ToggleSwitchSm]") {
    auto stable_cb = [](hal::SwitchState)-> void{};
    auto bouncing_cb = []()-> void{};
    auto read_state = []()-> hal::SwitchState {
        return hal::SwitchState::kOff;
    };
    thermo::ToggleSwitchSm::Context ctx(
        stable_cb, bouncing_cb, read_state
        ) ;
    boost::sml::sm<ToggleSwitchSm> sm{ctx};
    ToggleSwitchSm::SwitchChangedEvent event;
    event.state_ = hal::SwitchState::kOn;
    sm.process_event(event);
}
