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

### CLion: work inside the container

CLion 2026.2 is wired to the **same** compose `dev` service through a Dev
Container, so the IDE pulls the pinned image and starts the container
automatically ("starts if necessary") and the toolchain, CMake, Ninja, and the
ESP-IDF Python venv all run **inside** it. No host-side ESP-IDF install is
required.

- **Definition:** `.devcontainer/devcontainer.json` (reuses
  `docker-compose.yml`; it never defines its own image).
- **Connect:** open the repo in CLion and accept the Dev Container prompt, or
  use *Remote Development → Dev Containers → docker-compose.yml → `dev`*. CLion
  pulls `marcpawl/esp-idf:20261119c` if it is not cached and starts the service
  on demand. Note that compose falls back to building the local `Dockerfile` if
  the pull fails, so run `./scripts/dev.py pull` first if you want that failure
  to be loud.
- **Start:** `./scripts/dev.py up` runs `docker compose up -d` for the `dev`
  service before connecting a backend or shell.
- **Toolchain/CMake:** select the container as the remote host (SSH into the
  Dev Container). `.idea/cmake.xml` has one profile per subproject: the
  firmware plus each of the eight `linux` executables. Every profile uses its
  own `cmake-build-*` directory, so the IDE and `idf.py` never share a CMake
  cache. `mcgowan/build_esp32s3/` and `<suite>/build_linux/` remain the
  canonical `idf.py` trees used by `scripts/dev.py build`/`flash`.
- **Why one CMake profile per subproject.** A CLion CMake profile has no way to
  point CMake at a *sub*directory — a profile *is* a project. The repo root is
  only an aggregator (`project(... NONE)`) that shells out to `idf.py`, so a
  root profile enables no C/C++ languages, emits no `compile_commands.json`,
  and yields no code model (no navigation, no refactorings). Every subproject
  is a standalone ESP-IDF project and ESP-IDF sets
  `CMAKE_EXPORT_COMPILE_COMMANDS` on by default
  (`$IDF_PATH/tools/cmake/project.cmake`), so each one produces a database that
  CLion turns into a full resolve configuration. The profiles coexist rather
  than being merged because the projects share sources (`mcgowan/src` is
  compiled by the firmware and the host suites; `components/*` by all of them)
  and genuinely need different defines/include paths — notably
  `hal_board` (Xtensa) vs `hal_stub` (host/linux). The active profile is chosen
  by the run configuration's `CONFIG_NAME`.

  > **Verified reality (2026-10-27): the profiles do not deliver that yet.**
  > `GENERATION_DIR` sets only the *build* directory; the *source* directory is
  > the single CMake workspace in `.idea/misc.xml`
  > (`CMakeWorkspace PROJECT_DIR="$PROJECT_DIR$"`), which is the repo root for
  > every profile. After a clean CLion rebuild the `cmake-build-*` trees
  > recorded `CMAKE_HOME_DIRECTORY=/project`, none contained a
  > `compile_commands.json`, and each codemodel named project
  > `esp32_sensors_subprojects` with only the aggregate custom targets —
  > all `UTILITY`, i.e. no compilable translation units. So every profile
  > builds the same root aggregator, `-DIDF_TARGET=...` is unused by
  > `project(... NONE)`, and CLion gets no code model from them. Fixing it
  > needs either one CLion project per subproject or a merged
  > `compile_commands.json` exposed at the root. Until then
  > `scripts/dev.py build` is the way to build, and navigation/refactorings are
  > not driven by these profiles.

#### Code model, navigation, and tooltips

The CMake profiles do not provide a code model. For hover documentation,
`Ctrl+Q`, and Go to Definition, use a compilation-database project:

1. Run `./scripts/dev.py build` so every subproject has a compile database.
2. Run `./scripts/dev.py clion-compdb`. It merges the subproject databases
   into `.clion/compile_commands.json` (git-ignored). The firmware and board
   databases come first, so a source built for both targets keeps the ESP-IDF
   include paths; host-only files keep their `linux` flags.
3. In CLion select *Remote Connections → ssh*. Use
   `ssh -p 2222 dev@localhost` with project directory
   `/project/.clion/compile_commands.json`, then choose *Open Project*. Wait
   for *Downloading IDE backend...* to finish. This direct path was verified
   on 2026-11-20.
4. In that project tree, double-click
   `components/espidfcpp/libespidfcpp/include/espidf_cpp/timer.hpp` and
   hover `esp_timer_start_once`.
5. The committed `.clion/.idea/misc.xml` already maps the content root to
   `/project`, so no *Change Project Root* step is normally needed. Leave the
   toolchain as *Use Default* in *Settings → Build, Execution, Deployment →
   Compilation Database*; it is the container-local System toolchain. Remote
   Host toolchains are not supported here.
6. After a rebuild, re-run `clion-compdb` and reload with `Ctrl+Shift+O`.
7. For a guided check, run `./scripts/dev.py clion-manual-test` on the host.
   It calls `dev.py down` then `dev.py up`, seeds `clion-archive`
   when a `~/Downloads/CLion-*.tar.gz` exists, starts CLion, and prompts
   after each manual step. It refuses to run when `CI` is set; see
   `scripts/AGENTS.md`.

ESP-IDF's Doxygen comments live in the resolved headers under `/opt/esp/idf`,
so no documentation import is needed: `esp_timer_start_once` is documented in
`/opt/esp/idf/components/esp_timer/include/esp_timer.h`. This is the verified
path; repeat the hover check after changing include paths or rebuilding the
database.

> Editing the image, mounts, or user? Change `docker-compose.yml` (and
> `Dockerfile`), **not** `devcontainer.json`. The Dev Container deliberately
> inherits everything from the compose service so the CLI and the IDE cannot
> drift apart.

#### Run configurations

Shared run configurations live under `.idea/runConfigurations/`. Eleven call
`scripts/dev.py`, so a single script stays the source of truth for container
commands; the rest are native CLion **CMake** run configurations.

**Run and Debug for every Linux executable.** One configuration exists per
`linux` project:

- `test-utility (linux ELF, container)`
- `test-hal-stub (linux ELF, container)`
- `test-hal-common (linux ELF, container)`
- `test-logic (linux ELF, container)`
- `test-espidf-cpp (linux ELF, container)`
- `test-simulator (linux ELF, container)`
- `test-esp_timer-stub (linux ELF, container)`
- `donny (linux ELF, container)`

Each builds through the matching `<subproject>_build` root target, then runs
the generated ELF from `build_linux`. `RUN_PATH`, not `TARGET_NAME`, carries the
executable path: the root is `project(... NONE)` and defines only `UTILITY`
wrappers around `idf.py`, so naming an ELF in `TARGET_NAME` makes CLion report
"cannot find target". Host binaries carry `-gdwarf-4 -ggdb -Og` and the CLion
backend runs inside the same container, so **Debug** works with breakpoints.
`PROJECT_NAME` in every CMake configuration must be the root `project()` name,
`esp32_sensors_subprojects`. `ClionRunConfigurationTests` pins one ELF config
per linux project and checks that every profile and root target resolves.

**Build one component from the Run widget.** One configuration exists per
component that owns a buildable ESP-IDF project:

- `Build utility component (container)` — `components/utility`
- `Build hal component (container)` — `components/hal`
- `Build logic component (container)` — `components/logic`
- `Build espidfcpp component (container)` — `components/espidfcpp`
- `Build simulator component (container)` — `components/simulator`

Each passes its directory to `scripts/dev.py build`, so `hal` builds all four
of its test projects in one go. `components/sml` is header-only, has no project
to discover, and intentionally has no configuration.

| Run configuration | Kind | What it does |
|---|---|---|
| `*_build` CMake configurations (four) | CMake | Build one subproject via its root aggregate target; see the "Verified reality" note above for why they do not index sources |
| Build firmware (container) | `scripts/dev.py build` | builds every subproject |
| `Build <component> component` (five) | `scripts/dev.py build components/<name>` | builds every project under one component; no config for header-only `sml` |
| Flash & monitor (container) | `scripts/dev.py flash` | `idf.py -C mcgowan -B mcgowan/build_esp32s3 -p $ESP32_PORT flash monitor` (default `/dev/ttyUSB0`) |
| Host unit tests (container) | `scripts/dev.py test-linux` | runs every discovered host test suite (below) in path order, or those in/under a directory argument |
| clang-format check (container) | `scripts/dev.py format-check` | `clang-format --dry-run --Werror` |
| menuconfig (container) | `scripts/dev.py menuconfig` | Kconfig UI |
| Container shell (ESP-IDF) | `scripts/dev.py shell` | login shell with the IDF env sourced (accepts trailing args to run a command) |

Set `ESP32_PORT` (e.g. `/dev/ttyACM0`) to target a different serial device.

> **Building is container-only.** See `docker/AGENTS.md` for the container-only
> build policy.

#### Shared vs. local `.idea` files

The files needed to open, build, and refactor the project are **tracked** so a
change to the shared configuration shows up in the change list:

- Committed (shared): `.idea/cmake.xml` (CMake profiles),
  `.idea/runConfigurations/*` (run/debug configurations), `.idea/codeStyles/*`
  (formatting used by refactors), `.idea/misc.xml`, `.idea/modules.xml`,
  `.idea/esp32_sensors.iml`, `.idea/vcs.xml`, `.idea/editor.xml`.
- Local only (git-ignored): `.idea/workspace.xml` and `.idea/shelf/` — these
  hold per-machine IDE state, not shared configuration.

The root `.gitignore` ignores `.idea/*` and then un-ignores the shared files
above; see the "IDE / editor" section there.



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
  - `components/espidfcpp/tests/test-espidf-cpp/` covers the `libespidfcpp`
    wrappers; what each test pins is described in
    `components/espidfcpp/AGENTS.md`.
  - `components/simulator/tests/test-simulator/` links `libsimulator`; the
    component is empty today, so the suite runs no cases yet. What it will
    cover is described in `components/simulator/AGENTS.md`.
- **On-target tests** are planned for hardware-dependent code (1-Wire/DS18B20
  reads, deep-sleep wake, Wi-Fi upload).
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
