# The simulator

This file documents `components/simulator/` — the host-side simulator component,
the `donny` emulated application, and the simulator's host test project.
Repository-wide guidance (the build, the workflow, the quality gates) is in the
root `AGENTS.md`; the layering rules are in the `platform_layers` skill
(`.continue/skills/platform_layers/SKILL.md`).

## Layout

- `libsimulator/` — the simulator component. It is **empty on purpose**: it
  exists so the simulator tree has a component of its own that the executable
  and its tests can require, and so new simulator code has one obvious home.
  Add `.cpp` files to its `SRCS` (and public headers to `INCLUDE_DIRS`);
  consumers already name the component, so nothing else changes.
- `exe/donny/` — the `linux`-emulated application. It is a standalone ESP-IDF
  project (`project(donny)`), so its ELF is `donny.elf`.
- `tests/test-simulator/` — the component's host test project: an ESP-IDF
  `linux` project built with Unity. It `REQUIRES libsimulator` and runs no
  cases yet, so the wiring is real while the suite stays green.

The component and its tests sit together here the way the HAL, wrapper, and
logic components sit with theirs. ESP-IDF scans a component directory only one
level deep, so a project that requires `libsimulator` must name
`components/simulator` on its `EXTRA_COMPONENT_DIRS` even though `components/`
is already there; the projects compute the entry from their own depth in the
tree.

## `libsimulator`

`libsimulator` targets the ESP-IDF `linux` target only. It simulates hardware,
so it must never be built into the chip firmware: `mcgowan` does not put
`components/simulator` on its component search path, and the component's
`CMakeLists.txt` fails at configure time if `IDF_TARGET` is anything but
`linux`. It has no requirements today; when it gains `.cpp` files it should
require only lower layers.

## `donny`

A plain (non-test) `linux`-target application that constructs
`thermo::Application<thermo::hal::BoardStub>`. It requires `hal_stub`,
`liblogic`, `libespidfcpp`, `libsimulator`, and the vendored `json` component
(its layer requirements are in the `platform_layers` skill).

`hal_board` cannot be linked on the linux target: it requires
`esp_driver_tsens`, whose Kconfig needs `CONFIG_SOC_TEMP_SENSOR_SUPPORTED`, so
the chain ends at `esp_hal_ana_conv`, a SoC-only component with no linux
implementation (`esp_driver_gpio`, by contrast, registers itself empty on
linux).

### Wiring

`exe/donny/CMakeLists.txt` names every grouping directory on
`EXTRA_COMPONENT_DIRS` (the repo-root `components/`, plus `hal`, `espidfcpp`,
`logic`, and the simulator tree itself), because ESP-IDF scans a component
directory one level deep. It also appends `third_party/json` explicitly (the
*component directory*, not the whole of `third_party/`, so the `sml` submodule's
own upstream `CMakeLists.txt` can never be discovered) and `main` names `json`
in its `REQUIRES`. It restricts `COMPONENTS` to `main`, `hal_stub`, and
`libsimulator`, and excludes `hal_board`, so the host target never tries to
build chip-only components.

`donny`'s build tree is `exe/donny/build_linux/`; the project name and the
`donny` subproject identifier are unchanged, so the aggregate target stays
`donny_build`.

### The console protocol (`exe/donny/main/console_task.{hpp,cpp}`)

Nothing on the linux target ever changes the hardware, so a *test driver* has
to: the console task is that channel. It reads one JSON **object** per line
from stdin, applies the fields it names to the `BoardStub`, and writes the
resulting state back as one JSON line on stdout.

- **Task shape.** Split the same way `LedTask` is: `ConsoleRunner` holds the
  behaviour and the `std::shared_ptr<hal::BoardStub>` (shared, never owned),
  and `ConsoleTask : espidf_cpp::Task<ConsoleRunner>` is the FreeRTOS task. It
  is started in `app_main` **before** the `Application` is constructed, so a
  driver can already drive the board while the application boots.
- **Partial documents.** Every input field is optional; a missing field leaves
  that piece of board state alone, so `{"config_button":"on"}` presses one pole
  without touching anything else. Output is always a complete snapshot.
- **Idempotent round trip.** Feeding the task's own output back in changes
  nothing and yields byte-identical output, so a driver can read the state,
  edit one field, and write it back without knowing which fields exist. Three
  reported fields are therefore **ignored on input** because nothing in the
  board's interface can set them: `wifi.initialized` (only `init()` sets it),
  `deep_sleep.last_s` (the only way to sleep is to end the process), and — see
  below — `wifi.ap` / `wifi.station` when the interface is down.
- **Fields.** `button`, `config_button`, `run_button` (`"on"`/`"off"`);
  `leds.{green,red}` (`"on"`/`"off"`, a JSON bool is accepted on input);
  `chip_temp_c_x100` (int16); `sensors` (an array of `{rom, temp_c_x100}`, a
  complete replacement of the bus, `null` temperature for a failed reading);
  `wifi.{initialized, ap_running, station_running, ap, station}`;
  `deep_sleep.last_s` (`null` until the board has slept).
- **WiFi config is start-only.** The HAL can record a configuration *solely* by
  starting the interface (`start_ap()` / `start_station()`), so naming a
  `wifi.ap` / `wifi.station` object starts it, and `*_running` alone
  starts/stops with the configuration already recorded. Per interface the
  configuration is resolved *before* the running state, so the result never
  depends on JSON key order. A stopped interface reports `null` for its
  configuration (that is the only state a driver can round-trip), and
  restarting it reuses the recorded one. Contradictory input (`ap` with
  `ap_running:false`) is an error.
- **Errors never kill the task.** A malformed line answers
  `{"error":"malformed JSON"}`; a rejected field answers
  `{"error":"...","state":{...}}` with the state after the fields that *did*
  apply, and the loop continues. EOF (the driver closed the pipe) makes the
  task idle on `vTaskDelay(60 s)` rather than spin.
- **stdout is the protocol; logs go to stderr.** ESP-IDF logs to stdout by
  default (`esp_log_vprint_func = &vprintf`) and the stub devices log when they
  are written to, which would interleave log lines with protocol output.
  `app_main` installs `esp_log_set_vprintf()` via a **static-init** struct:
  doing it inside `app_main` is too late, since the port logs "Starting
  scheduler" first.
- **stdin is read with `read()`, never `fgets`.** The ESP-IDF `linux` FreeRTOS
  port drives the scheduler from a 1 kHz `SIGALRM` whose handler it installs
  with `sa_flags = 0` (no `SA_RESTART`), so *every* blocking read returns early
  with `EINTR`. `fgets` cannot distinguish that from end of file: it sets the
  sticky error flag and returns `NULL`, so an interactive console saw "EOF"
  about a millisecond after it started and shut its input down (a pipe only
  appeared to work because the data was already buffered). `read()` keeps
  `errno`, so the console task simply retries on `EINTR`; reading the
  descriptor directly also sidesteps the `FILE` error flag.
- **The JSON library is vendored in `third_party/`.** `third_party/json` holds
  `nlohmann/json` (v3.11.3, single header, MIT + `LICENSE.MIT`) as a
  header-only ESP-IDF component. It sits in `third_party/` rather than under
  `components/` or the simulator tree because `third_party/` is outside every
  directory `scripts/dev.py` clang-formats: a vendored header must stay
  byte-identical to upstream so it can be diffed against a release. The
  firmware deliberately does not use it: it formats its payload by hand and has
  no JSON parser, whereas `donny`'s binary size is irrelevant. `donny` builds
  with `CONFIG_COMPILER_CXX_EXCEPTIONS=n`, so the parse uses the non-throwing
  form (`json::parse(line, nullptr, false)` + `is_discarded()`), never
  `try`/`catch`.

## The host tests

`tests/test-simulator/` is a standalone `linux` project whose `main` requires
`libsimulator` (and privately `unity`). The component is empty today, so
`test_main.cpp` registers no cases; `unity_run_all_tests()` handles the empty
set and the suite passes. Add `TEST_CASE`s in new `main/*.cpp` sources and list
them in `main/CMakeLists.txt`; `WHOLE_ARCHIVE` keeps their registrations linked
into the ELF.

Build and run the suite with `scripts/dev.py test-simulator`, or directly in
the container:

```bash
scripts/dev.py shell -C components/simulator/tests/test-simulator \
    idf.py -B build_linux --preview set-target linux
scripts/dev.py shell -C components/simulator/tests/test-simulator \
    idf.py -B build_linux build
./components/simulator/tests/test-simulator/build_linux/test-simulator.elf
```
