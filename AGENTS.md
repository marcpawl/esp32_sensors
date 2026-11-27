# Repository Overview

## Project Description

ESP32 project.

esp-idf is being used. esp target for production, and linux target
for testing.

C++26 with modern practices.  See the skill cpp-coding-standards and cpp_qc
when modifying C++ code. CMake build files.

Python is the preferred scripting language.  See the skill python_qc
when modifying python code.

**Key technologies:**

- Host OS : Linux Mint
- IDE : CLion 2026.2 
-  Container runtime : Docker. 
- Container image :  See docker/docker-compose.yml
- Target framework : ESP-IDF v6.1 
- Toolchain : GCC 15.2.0 (Xtensa `esp-15.2.0_20251204`, bundled with ESP-IDF v6.1) |

## Directory Structure

```
esp32_sensors/
├── CMakeLists.txt       # Root aggregator (plain CMake) — builds all subprojects
├── mcgowan/             # Production Firmware app_main
├── components/          # Reusable ESP-IDF components 
│   ├── utility/         #   The lowest layer: fixed-capacity containers (static_vector/string)
│   ├── hal/             #   The hardware-abstraction layer (see "HAL" section)
│   ├── espidfcpp/       #   C++ wrappers over ESP-IDF/FreeRTOS (Task, queue, handles, ...)
│   ├── logic/           #   Board-independent business logic
│   ├── simulator/       #   Host-side simulator (IDF `linux`)
│   └── sml/             #   Wrapper component exposing the third_party/sml submodule
├── third_party/         # Vendored third-party code (never formatted, never tidy'd)
│   ├── json/            #   nlohmann/json v3.11.3 (header-only, MIT)
│   └── sml/             #   boost-ext/sml git SUBMODULE, pinned to v1.2.0
├── docker/              # Dev container: Dockerfile + docker-compose.yml
├── .devcontainer/       # CLion / VS Code Dev Container definition (reuses compose)
├── .opencode/           # OpenCode config: `skills/` (one SKILL.md per topic)
│   ├── skills/          #   cpp-coding-standards, git, platform_layers,
│   │                    #   quality_checks, cmake, markdown, cpp_qc,
│   │                    #   python_qc, bug_found
│   └── commands/        #   slash commands (e.g. review)
├── scripts/dev.py       # Container task runner (build/flash/test/format/shell)
├── scripts/AGENTS.md    #   dev.py and test_dev.py documentation
├── scripts/idf.sh       # Runs idf.py with the ESP-IDF env, sourcing export.sh if absent
├── scripts/test_dev.py  # Host unit tests for dev.py (`dev.py self-check`)
├── scripts/dev.py-completion.bash  # Bash tab-completion for scripts/dev.py
├── build_root/          # Root aggregator CMake build tree (git-ignored)
├── */cmake-build-*/     # CLion's own per-subproject build trees (git-ignored; see .idea/cmake.xml)
├── .dockerignore        # Build-context exclusions
├── .gitignore           # Excludes build dirs, sdkconfig, local IDE state
├── AGENTS.md            # This file — guidance for AI agents and developers
├── LICENSE              # GNU General Public License v3.0
└── .idea/               # CLion config (shared cmake.xml + run configs tracked; workspace.xml local)
```

**The root is not an ESP-IDF project.** The top-level `CMakeLists.txt` is plain
CMake that only aggregates the subprojects, so a single build covers
everything. Each subproject is a standalone ESP-IDF project with its own
`CMakeLists.txt`, `project()`, `sdkconfig.defaults`, and a build
directory **inside its own folder**, named after the target:


**The state-machine library is a git submodule.** `third_party/sml` is
[boost-ext/sml](https://github.com/boost-ext/sml) at tag **v1.2.0** (commit
`2d8485d`), recorded in `.gitmodules` and used by the `liblogic` component for
the application state machines. `components/sml` is a thin ESP-IDF wrapper that
exposes the submodule's `include/` directory; `liblogic` names `sml` in
`REQUIRES`, so consumers include `<boost/sml.hpp>` and never name the component
themselves. The upstream tree is **never** a component: its own `CMakeLists.txt`
builds upstream tests/examples, so it stays off every component search path
(`components/simulator/exe/donny/CMakeLists.txt` names `third_party/json`
explicitly instead of scanning `third_party/`), and it is outside `ROOTS`, so
clang-format never touches it (`_is_vendored` keeps clang-tidy off it too).

- **Not vendored, not committed content.** The submodule is pinned by the
  gitlink recorded in git, so a fresh clone has an empty directory until the
  sources are fetched. `scripts/dev.py submodules` fetches them
  (`git submodule update --init --recursive -- <path>`, idempotent), and
  `build`, `test`, and `clang-tidy` call the same helper first, so a bare clone
  builds without a manual step. CI runs `submodules` as its own step, before
  `build`, so a failed fetch is reported on its own rather than as a confusing
  configure error.
- **Updating the pin:** `git -C third_party/sml checkout v1.2.1`, then
  `git add third_party/sml` in this repo (and update the version in this
  document and `components/sml/CMakeLists.txt`). `scripts/dev.py self-check`
  pins the gitlink to the v1.2.0 commit.

## Development Workflow

> **Important:** All builds run inside the ESP-IDF Docker environment. The
> environment is defined by the committed `Dockerfile` + `docker-compose.yml`,
> but the image itself is **prebuilt and pulled**
>  `./scripts/dev.py shell <cmd>` or other command to execute actions
> such as build.

### Development environment setup

The Docker dev environment, image pinning, ccache handling, and
container-only builds are documented in `docker/AGENTS.md`.

### IDE: CLion

CLion setup, the merged compilation-database workflow for navigation and
Doxygen tooltips, run/debug configurations, and shared IDE files are in
the `clion` skill (`.opencode/skills/clion/SKILL.md`).


### python scripts/dev.py

The container task runner and its command table are documented in
`scripts/AGENTS.md`. Run `./scripts/dev.py --help` for the live
list.

### Reuse one container shell

The first time a shell is needed, open one with `./scripts/dev.py shell`.
Run every subsequent command in that shell rather than calling
`./scripts/dev.py shell <cmd>` again. Each wrapper call starts a new
container and discards it when the command exits, so environment changes,
the working directory, and anything built up interactively are lost between
calls; one open shell keeps them and avoids paying the container startup
cost every time.



### Flash and monitor

The host `/dev` tree is bind-mounted into the container. The board can be
plugged in at any time and it will be accessible by the container.

```bash
./scripts/dev.py flash
```

### Testing approach

There are two kinds of test suite, runnable from the host via `scripts/dev.py`:

- **Host-based unit tests** build for the ESP-IDF `linux` target with Unity —
  no hardware needed — in seven projects:

  - `components/utility/tests/test-utility/` — the shared fixed-capacity
    containers (`static_vector`, `static_string`); what each test pins is
    described in `components/utility/AGENTS.md`.
  - `test-hal-stub/` — the pin-free `hal_stub` devices; what each test pins is
    described in `components/hal/AGENTS.md`.
  - `test-hal-common/` — the implementation-free `hal_common` vocabulary; what
    each test pins is described in `components/hal/AGENTS.md`.
  - `components/hal/tests/esp_timer/stub/` — the host (`linux`) `esp_timer`
    suite against `hal_stub`, sharing its cases with the on-target board suite;
    what each test pins is described in `components/hal/AGENTS.md`.
  - `components/logic/tests/test-logic/` covers the pure-logic modules
    (`temp_format`, `limits`, `led_manager`, `payload`, `app_config`
    validation, `rom_id`, `rtc_buffer` — including the cold-boot
    `rtc_state_reset()`, and the `mode_selection` pole mapping) and the
    firmware sources in `mcgowan/src` that need no chip driver
    (`app_config.cpp`, `config_page.cpp`, `power_source.cpp`); what each test
    pins is described in `components/logic/AGENTS.md`.
  - `components/espidfcpp/tests/test-espidf-cpp-linux/` covers the `libespidfcpp`
    wrappers; what each test pins is described in
    `components/espidfcpp/AGENTS.md`.

  - `components/espidfcpp/tests/test-espidf-cpp-esp32/` builds the same shared
    `test-espidf-cpp-lib` cases for ESP32-S3; what each test pins is described
    in `components/espidfcpp/AGENTS.md`.
  - `components/simulator/tests/test-simulator/` links `libsimulator`; the
    component is empty today, so the suite runs no cases yet. What it will
    cover is described in `components/simulator/AGENTS.md`.
- **On-target tests** are built, flashed, and run through `scripts/dev.py test-esp32`:
  `components/espidfcpp/tests/test-espidf-cpp-esp32/` runs the shared wrapper
  cases on ESP32-S3 and `components/hal/tests/esp_timer/board/` runs the timer
  cases. Coverage for hardware-dependent code (1-Wire/DS18B20 reads, deep-sleep
  wake, Wi-Fi upload) is still planned.
- Keep hardware interactions behind thin interfaces so business logic remains
  host-testable. When adding pure logic that the firmware and the tests both
  need, put it in `components/logic/liblogic` (headers under `include/logic/`,
  `.cpp` files in its `SRCS`); when it is firmware-only, add it to both
  `mcgowan/main/CMakeLists.txt` and
  `components/logic/tests/test-logic/main/CMakeLists.txt`.

Run every host suite with `./scripts/dev.py test-linux`, which stops at the
first failure; `./scripts/dev.py test-linux <directory>` runs only the suites
in or under that directory, so one suite is
`./scripts/dev.py test-linux components/utility/tests/test-utility`.

Linux builds run this repository's C++ with UBSan and ASan. See the `cmake`
skill for the helper modules, target filtering, runtime linking, and why
`build --analyzer` skips instrumentation. CI keeps the two builds separate so
the host suites execute instrumented code.

The on-target `esp_timer` suite runs with `./scripts/dev.py test-esp32` (or
`./scripts/dev.py test-esp32 components/hal/tests/esp_timer/board`): it builds
the ESP32-S3 project, flashes it, and waits up to 120 seconds for the suite's
sentinel on the serial port. See the `quality_checks` skill for the gates and
the fixing policy.

### Quality checks

Run every quality gate through `scripts/dev.py` and fix what it reports. The
gates — clang-format, pycodestyle, clang-tidy, the build, the seven host test
suites, and the `dev.py` self-check — their order, and the fixing policy are
documented in the `quality_checks` skill
(`.opencode/skills/quality_checks/SKILL.md`). Quality gates are run via
`scripts/dev.py`; see the `quality_checks`, `cpp_qc`, and `python_qc` skills
for the required subsets.

Compiler warnings are errors in this repository's own code.
`cmake/compiler-warnings.cmake` re-enables the categories ESP-IDF downgrades
with `-Wno-error=...` on our component targets only, so ESP-IDF and
`third_party/` keep their upstream defaults while a warning in our sources
fails `scripts/dev.py build`. When a warning points into code we do not own,
suppress it at the offending `#include` with a diagnostic pragma — see the
`cpp-coding-standards` skill — rather than relaxing the flags.

### CMake

How the `CMakeLists.txt` files are written — including the rule that they
document the build and never the sources — is in the `cmake` skill
(`.opencode/skills/cmake/SKILL.md`). Read it before editing a build file or
adding a source.

### Markdown

How the Markdown files are written — the line width, and the preference for
lists and subsections over sentences that run several items together — is in
the `markdown` skill (`.opencode/skills/markdown/SKILL.md`). Read it before
writing or editing a `.md` file.


# Agent interaction

## New tasks

bug_found task describes how to handle finding a new bug while
in the process of completing a task.

## Prompting
  If more than five minutes has passed, and you have just outputted something 
  that requires a user response outputted DONE! then ring the bell
  five times.

## Verbosity

  Always explain the plan when asked to do something.  Explain the
  step before you perform the work for it.
