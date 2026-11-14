---
name: platform_layers
description: The repository's platform layers and their dependency rules. Use
  when adding, moving, or reviewing a component or test project, when choosing
  which HAL implementation or ESP-IDF dependency to link, or when listing
  components and tests.
---

# Platform layers

The code is organized in layers. A layer may use the layers below it; a lower
layer must never use a higher one. The rule applies to both CMake requirements
(`REQUIRES` / `PRIV_REQUIRES`) and `#include`s — a header-only dependency is
still a dependency.

The hardware-abstraction layer (layers 1 and 2 below) is documented in
`components/hal/AGENTS.md`; this skill owns the rules that connect the layers.

## The layers

| Layer | Components | Test / application projects |
|---|---|---|
| 1 (lowest) | `hal_common` | `test-hal-common` |
| 2 | `hal_board`, `hal_stub` | `test-hal-stub` |
| 3 | `libespidfcpp` | `test-espidf-cpp` |
| 4 | `liblogic` | `test-logic` |
| 5 | `libsimulator` | `test-simulator` |
| 6 (highest) | — | `mcgowan`, `donny` |

`hal_common` is header-only.

`hal_board` is the GPIO-backed implementation (ESP32-S3);

`hal_stub` is the pin-free implementation (`linux`).

`libespidfcpp` provides C++ wrappers around esp-idf code, to make usage less
error prone.

`liblogic` holds the main application business logic.

`libsimulator` is the host-side simulator component (`linux` only). It is empty
today and must never be required by a chip-target project.

`mcgowan` is the ESP32-S3 firmware.

`donny` is the `linux`-emulated application.

## Dependency direction

- Layer N may require layers `< N`, never `> N`.
- Never include a higher layer's header, even a header-only one.
- A higher layer may freely use the lower layers: `liblogic` uses the
  `hal_common` vocabulary, and `mcgowan` links `hal_board` and `liblogic`.
- third_part may be used by any layer.

## Platform rules

| Target | May link | Must not link |
|---|---|---|
| ESP32-S3 (`mcgowan`) | `hal_board` | `hal_stub` |
| `linux` (`test-hal-common`, `test-hal-stub`, `test-logic`, `test-espidf-cpp`, `test-simulator`, `donny`) | `hal_stub` | `hal_board` |
| Platform-neutral components (`hal_common`, `libespidfcpp`, `liblogic`) | neither | both |

`libespidfcpp` and `liblogic` build for both the chip and the host, so they
must not name either HAL implementation. They use the `hal_common` vocabulary
instead and stay implementation-agnostic with templates and concepts: for
example `logic/led_task.hpp` is templated on `hal::BoardLike`, and `mcgowan`
supplies `hal::board::Board` while the host tests supply `hal::stub::BoardStub`.

`hal_common` is shared by everything and is never excluded.

## Walk order

When writing or editing a list of components or test projects, go from the
lowest layer to the highest:

- Components: `hal_common`, `hal_board`, `hal_stub`, `libespidfcpp`,
  `liblogic`, `libsimulator`.
- Test projects: `test-hal-common`, `test-hal-stub`, `test-espidf-cpp`,
  `test-logic`, `test-simulator`.
- Applications: `mcgowan`, `donny`.

`./scripts/dev.py test` currently runs `test-logic` before `test-espidf-cpp`
and `test-simulator`; that is the command's existing order, not the layer order
to use for a new list.

## Current dependency edges

| Component / project | Repository-layer requirements |
|---|---|
| `hal_common` | none (ESP-IDF `esp_common` only) |
| `hal_board` | `hal_common` |
| `hal_stub` | `hal_common` |
| `libespidfcpp` | none (ESP-IDF `esp_http_server` only) |
| `liblogic` | `libespidfcpp`, `hal_common`, `sml` |
| `libsimulator` | none yet |
| `mcgowan` | `hal_board`, `liblogic`, `libespidfcpp` |
| `donny` | `hal_stub`, `liblogic`, `libespidfcpp`, `libsimulator`, vendored `json` |
| `test-hal-common` | `hal_common`; privately `hal_stub` |
| `test-hal-stub` | `hal_stub` |
| `test-espidf-cpp` | `libespidfcpp`; privately `hal_stub` |
| `test-logic` | `liblogic`; privately `libespidfcpp`, `hal_stub` |
| `test-simulator` | `libsimulator`; privately `unity` |

Test projects may privately link `hal_stub` even when their component is
platform-neutral: `test-hal-common` needs the concrete device types because the
`hal_common` concepts name them, and `test-espidf-cpp` keeps the requirement
for parity and future HAL-facing tests. This is a test-project allowance, not a
precedent for a component — `libespidfcpp` and `liblogic` themselves must
never require either implementation.

## Where the rule is enforced

- Each subproject's top-level `CMakeLists.txt` points `EXTRA_COMPONENT_DIRS` at
  the repo-root `components/` (the `sml` wrapper), at `components/hal/` (the
  three HAL components), at `components/espidfcpp/` (the ESP-IDF wrappers), at
  `components/logic/` (the application logic) and — where the simulator is
  linked — at `components/simulator/` (the simulator component), because its
  own project root is not on the default search path. All entries are needed:
  ESP-IDF scans a component directory one level deep
  (`__project_component_dir` globs `dir/*` and does not descend), so a grouping
  directory is only found when it is named on its own. Each project also sets
  `EXCLUDE_COMPONENTS` before `project()`: `mcgowan` excludes `hal_stub`; every
  `linux` project excludes `hal_board`. `hal_common` is never excluded.
- The `linux` projects also narrow `set(COMPONENTS ...)` to `main` plus
  `hal_stub` (and `liblogic` in `test-logic`, so the component is built now
  that it has `.cpp` files; `libsimulator` in `test-simulator` and `donny`), so
  the host target does not try to build chip-only ESP-IDF components.
- A component's requirements live in its own `CMakeLists.txt` — under
  `components/hal/<name>/` for the HAL components, under
  `components/espidfcpp/libespidfcpp/` for the ESP-IDF wrappers, under
  `components/logic/liblogic/` for the application logic, under
  `components/simulator/libsimulator/` for the simulator, and under
  `components/<name>/` for the rest. An application's live in
  `<project>/main/CMakeLists.txt` (`donny`'s in
  `components/simulator/exe/donny/main/CMakeLists.txt`). Use `REQUIRES` when the dependency appears
  in public headers, `PRIV_REQUIRES` when only `.cpp` files use it.
- The component *name* is the directory name, not its path:
  `components/hal/hal_common` is the component `hal_common`, so every
  `REQUIRES`, `PRIV_REQUIRES`, `EXCLUDE_COMPONENTS` and `set(COMPONENTS ...)`
  names the component and needs no change when a component moves.
- `scripts/dev.py` must keep `components` in `ROOTS`: it is the pathspec
  clang-format walks and the source of the clang-tidy header filter, so a
  component left out of it is silently unformatted and unanalyzed.
  `HalComponentCoverageTests` in `scripts/test_dev.py` pins that.

## Adding or moving code

1. Place the new code in a layer first, then declare only downward
   requirements.
2. If it needs something from a higher layer, move the code up or invert the
   dependency. A template parameter constrained by a `hal_common` concept is
   the usual answer — see `logic::LedTask<Board>`.
3. Never make `libespidfcpp`, `liblogic`, or `libsimulator` depend on
   `hal_board` or `hal_stub`; that breaks one of the two builds. `libsimulator`
   must not be built for the chip at all.
4. Before moving an existing class to another component, check what it depends
   on and what depends on it. If a dependency would have to move too, ask the
   user before starting: dependencies may need to move in their own steps
   first.
