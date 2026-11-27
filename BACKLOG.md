# Agents do not execute this, until instructed by a HUMAN.

Is .dockerignore still needed?

SoftSwitch should be in its own library

Add test for espidf_cpp::Switch
SoftSwitch should be used in Switch tests.

Move Board to logic

Figure out how to detect if stubs no longer match
the real declaration.
e.g. components/hal/hal_stub/include/driver/gpio.h

Uninstall OpenCode on gamer, not working


Doc updates needed

- .opencode/skills/platform_layers/SKILL.md: the platform rules, "Adding or
  moving code" item 3, the test-project allowance paragraph, and the
  dependency-edge table all say libespidfcpp/liblogic must never require
  hal_stub. Both now privately require hal_stub on linux (esp_driver_gpio on
  chip) for the GPIO mock.
- components/logic/AGENTS.md: "Dependencies" says liblogic must never require
  hal_stub, and "Keep it that way" says IDF calls stay behind logic. switch.cpp
  now calls the GPIO driver directly and the component has the conditional
  GPIO dependency. Also add switch.cpp to the "Adding sources" list.
- components/espidfcpp/AGENTS.md: add the dependency notes for public
  hal_common and the private target-conditional GPIO dependency (the
  interrupt_handle.hpp row is already there).
- components/hal/AGENTS.md: InterruptHandle is now also an espidfcpp wrapper,
  not only hal_board/interrupt_handle.hpp.
- components/hal/hal_common/include/hal/gpio.hpp and its copy in
  hal_stub/include/hal/gpio.hpp: the comment says hal_board is the only place
  that calls the driver; logic/switch.cpp and espidfcpp/interrupt_handle.cpp
  now do too.
- components/hal/hal_stub/CMakeLists.txt: the comment says the mock
  driver/gpio.h is included by "the logic sources"; espidfcpp sources include
  it too.
