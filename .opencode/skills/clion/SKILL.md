---
name: clion
description: Configure CLion for this ESP-IDF repository, use the merged
  compilation database for navigation and Doxygen tooltips, run or debug the
  host executables, and understand which IDE files are shared. Use when
  opening the project in CLion, changing IDE or run configurations, or
  diagnosing a missing code model or tooltip.
---

# CLion in this repository

CLion 2026.2 connects to the same compose `dev` service through a Dev
Container. The IDE pulls the pinned image and starts the container on demand;
the toolchain, CMake, Ninja, and the ESP-IDF Python venv all run inside it. No
host-side ESP-IDF install is required.

## Dev Container connection

- **Definition:** `.devcontainer/devcontainer.json` reuses
  `docker/docker-compose.yml`; it never defines its own image.
- **Connect:** open the repository in CLion and accept the Dev Container
  prompt, or use *Remote Development → Dev Containers → docker-compose.yml →
  `dev`*. CLion pulls `marcpawl/esp-idf:20261119c` if it is not cached. Compose
  falls back to building the local `Dockerfile` when the pull fails, so run
  `./scripts/dev.py pull` first if that failure must be loud.
- **Start and stop:** `./scripts/dev.py up` runs `docker compose up -d` before
  connecting a backend or shell. `./scripts/dev.py down` runs
  `docker compose down --remove-orphans` for a clean container and a fresh
  Toolbox log.
- **Toolchain/CMake:** select the container as the remote host (SSH into the
  Dev Container). `.idea/cmake.xml` has one profile per subproject: the
  firmware plus each of the eight `linux` executables. Each profile uses its
  own `cmake-build-*` directory, so the IDE and `idf.py` never share a CMake
  cache. `mcgowan/build_esp32s3/` and `<suite>/build_linux/` remain the
  canonical `idf.py` trees used by `scripts/dev.py build`/`flash`.

### Why one CMake profile per subproject

A CLion CMake profile cannot point CMake at a *sub*directory: a profile *is* a
project. The repository root is only an aggregator (`project(... NONE)`) that
shells out to `idf.py`, so a root profile enables no C/C++ languages, emits no
`compile_commands.json`, and yields no code model. Every subproject is a
standalone ESP-IDF project, and ESP-IDF sets `CMAKE_EXPORT_COMPILE_COMMANDS`
on by default (`$IDF_PATH/tools/cmake/project.cmake`), so each one produces a
database CLion can use.

The profiles coexist because the projects share sources (`mcgowan/src` is
compiled by the firmware and the host suites; `components/*` by all of them)
and need different defines and include paths — notably `hal_board` (Xtensa)
versus `hal_stub` (host/linux). The active profile is chosen by the run
configuration's `CONFIG_NAME`.

> **Verified reality (2026-10-27): the profiles do not deliver a code model.**
> `GENERATION_DIR` sets only the *build* directory; the *source* directory is
> the single CMake workspace in `.idea/misc.xml`
> (`CMakeWorkspace PROJECT_DIR="$PROJECT_DIR$"`), which is the repository root
> for every profile. After a clean CLion rebuild the `cmake-build-*` trees
> recorded `CMAKE_HOME_DIRECTORY=/project`, none contained a
> `compile_commands.json`, and each codemodel named project
> `esp32_sensors_subprojects` with only aggregate `UTILITY` custom targets.
> Every profile therefore builds the same root aggregator, and `-DIDF_TARGET`
> is unused by `project(... NONE)`. Use `scripts/dev.py build` to build; use
> the merged compilation database described below for navigation.

> Editing the image, mounts, or user? Change `docker/docker-compose.yml` (and
> `Dockerfile`), **not** `devcontainer.json`. The Dev Container deliberately
> inherits everything from the compose service so the CLI and the IDE cannot
> drift apart.

## Code model, navigation, and tooltips

The CMake profiles do not provide a code model. For hover documentation,
`Ctrl+Q`, and Go to Definition, open a compilation-database project:

1. Run `./scripts/dev.py build` so every subproject has a compile database.
2. Run `./scripts/dev.py clion-compdb`. It merges the subproject databases
   into `.clion/compile_commands.json` (git-ignored). The firmware and board
   databases come first, so a source built for both targets keeps the ESP-IDF
   include paths; host-only files keep their `linux` flags.
3. In CLion select *Remote Connections → ssh*. Use
   `ssh -p 2222 dev@localhost` with project directory
   `/project/.clion/compile_commands.json`, then choose *Open Project*.
   Without a seeded backend archive, wait for *Downloading IDE backend...* to
   finish. With one, Toolbox reuses the local file; see `clion-archive` in
   `scripts/AGENTS.md`. This direct path was verified on 2026-11-20.
4. In that project tree, double-click
   `components/espidfcpp/libespidfcpp/include/espidf_cpp/timer.hpp` and hover
   `esp_timer_start_once`.
5. The committed `.clion/.idea/misc.xml` maps the content root to `/project`,
   so no *Change Project Root* step is normally needed. Leave the toolchain as
   *Use Default* in *Settings → Build, Execution, Deployment → Compilation
   Database*; in the container it is the local System toolchain. Remote Host
   toolchains are not supported here.
6. After a rebuild, re-run `clion-compdb` and reload with `Ctrl+Shift+O`.
7. For a guided check, run `./scripts/dev.py clion-manual-test` on the host.
   It calls `dev.py down`, then `dev.py up`, seeds `clion-archive` when a
   `~/Downloads/CLion-*.tar.gz` exists, starts CLion, checks the Toolbox log
   for local archive reuse, and prompts after each manual step. It refuses to
   run when `CI` is set; see `scripts/AGENTS.md`.

ESP-IDF's Doxygen comments live in the resolved headers under `/opt/esp/idf`,
so no documentation import is needed: `esp_timer_start_once` is documented in
`/opt/esp/idf/components/esp_timer/include/esp_timer.h`. Repeat the hover
check after changing include paths or rebuilding the database.

## Run configurations

Shared run configurations live under `.idea/runConfigurations/`. Eleven call
`scripts/dev.py`, so a single script stays the source of truth for container
commands; the rest are native CLion **CMake** run configurations.

### Run and Debug for every Linux executable

One configuration exists per `linux` project:

- `test-utility (linux ELF, container)`
- `test-hal-stub (linux ELF, container)`
- `test-hal-common (linux ELF, container)`
- `test-logic (linux ELF, container)`
- `test-espidf-cpp-linux (linux ELF, container)`
- `test-simulator (linux ELF, container)`
- `test-esp_timer-stub (linux ELF, container)`
- `donny (linux ELF, container)`

Each builds through the matching `<subproject>_build` root target, then runs
the generated ELF from `build_linux`. `RUN_PATH`, not `TARGET_NAME`, carries
the executable path: the root is `project(... NONE)` and defines only
`UTILITY` wrappers around `idf.py`, so naming an ELF in `TARGET_NAME` makes
CLion report "cannot find target". Host binaries carry `-gdwarf-4 -ggdb -Og`
and the CLion backend runs inside the same container, so **Debug** works with
breakpoints. `PROJECT_NAME` in every CMake configuration must be the root
`project()` name, `esp32_sensors_subprojects`. `ClionRunConfigurationTests`
pins one ELF configuration per linux project and checks that every profile and
root target resolves.

### Build one component from the Run widget

One configuration exists per component owning a buildable ESP-IDF project:

- `Build utility component (container)` — `components/utility`
- `Build hal component (container)` — `components/hal`
- `Build logic component (container)` — `components/logic`
- `Build espidfcpp component (container)` — `components/espidfcpp`
- `Build simulator component (container)` — `components/simulator`

Each passes its directory to `scripts/dev.py build`, so `hal` builds all four
of its test projects in one go. `components/sml` is header-only, has no
project to discover, and intentionally has no configuration.

| Run configuration | Kind | What it does |
|---|---|---|
| `*_build` CMake configurations (four) | CMake | Build one subproject via its root aggregate target; see the verified-reality note above for why they do not index sources |
| Build firmware (container) | `scripts/dev.py build` | builds every subproject |
| `Build <component> component` (five) | `scripts/dev.py build components/<name>` | builds every project under one component; no config for header-only `sml` |
| Flash & monitor (container) | `scripts/dev.py flash` | `idf.py -C mcgowan -B mcgowan/build_esp32s3 -p $ESP32_PORT flash monitor` (default `/dev/ttyUSB0`) |
| Host unit tests (container) | `scripts/dev.py test-linux` | runs every discovered host test suite in platform-layer order, or those in/under a directory argument |
| clang-format check (container) | `scripts/dev.py format-check` | `clang-format --dry-run --Werror` |
| menuconfig (container) | `scripts/dev.py menuconfig` | Kconfig UI |
| Container shell (ESP-IDF) | `scripts/dev.py shell` | login shell with the IDF env sourced (accepts trailing args to run a command) |

Set `ESP32_PORT` (e.g. `/dev/ttyACM0`) to target a different serial device.

> **Building is container-only.** See `docker/AGENTS.md` for the policy.

## Shared versus local IDE files

Files needed to open, build, and refactor the project are **tracked** so a
shared configuration change shows up in the change list:

- Root `.idea/`: `.idea/cmake.xml`, `.idea/runConfigurations/*`,
  `.idea/codeStyles/*`, `.idea/misc.xml`, `.idea/modules.xml`,
  `.idea/esp32_sensors.iml`, `.idea/vcs.xml`, and `.idea/editor.xml`.
- Compilation-database project `.clion/.idea/`: `.gitignore`, `editor.xml`,
  `misc.xml`, and `vcs.xml`. Its `misc.xml` maps `CompDBWorkspace` to
  `/project`; the generated `compile_commands.json` stays ignored.
- Local only (git-ignored): `.idea/workspace.xml`, `.idea/shelf/`, and
  `.clion/.idea/workspace.xml` — per-machine IDE state, not shared config.

The root `.gitignore` ignores `.idea/*` and then un-ignores the shared files;
see its "IDE / editor" section.
