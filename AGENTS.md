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
├── .continue/           # Continue CLI config: `rules/` (slash commands), `skills/`
│   └── skills/          #   one SKILL.md per topic: cpp-coding-standards, git,
│                        #   platform_layers, quality_checks, cmake, markdown,
│                        #   cpp_qc, python_qc
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
  pulls `marcpawl/esp-idf:20261109b` if it is not cached and starts the service
  on demand. Note that compose falls back to building the local `Dockerfile` if
  the pull fails, so run `./scripts/dev.py pull` first if you want that failure
  to be loud.
- **Toolchain/CMake:** select the container as the remote host (SSH into the
  Dev Container). CLion configures and builds **each subproject** under its own
  CMake build directory (all from `.idea/cmake.xml`):
  `mcgowan/cmake-build-esp32s3/`, `test/cmake-build-linux-test/`,
  `espidf-cpp-test/cmake-build-linux-espidf-cpp-test/`,
  `donny/cmake-build-linux-donny/` — so the IDE and `idf.py` never share a
  CMake cache. `mcgowan/build_esp32s3/` remains the canonical `idf.py` build
  used by `scripts/dev.py build`/`flash`.
- **Why one CMake profile per subproject.** A CLion CMake profile has no way to
  point CMake at a *sub*directory — a profile *is* a project. The repo root is
  only an aggregator (`project(... NONE)`) that shells out to `idf.py`, so a
  root profile enables no C/C++ languages, emits no `compile_commands.json`,
  and yields no code model (no navigation, no refactorings). Every subproject
  is a standalone ESP-IDF project and ESP-IDF sets
  `CMAKE_EXPORT_COMPILE_COMMANDS` on by default
  (`$IDF_PATH/tools/cmake/project.cmake`), so each one produces a database that
  CLion turns into a full resolve configuration. The four profiles coexist
  rather than being merged because the projects share sources (`mcgowan/src` is
  compiled by both `mcgowan` and `test`; `components/*` by three or four of
  them) and genuinely need different defines/include paths — notably
  `hal_board` (Xtensa) vs `hal_stub` (host/linux). The active profile is chosen
  by the run configuration's `CONFIG_NAME`.

  > **Verified reality (2026-10-27): the profiles do not deliver that yet.**
  > `GENERATION_DIR` sets only the *build* directory; the *source* directory is
  > the single CMake workspace in `.idea/misc.xml`
  > (`CMakeWorkspace PROJECT_DIR="$PROJECT_DIR$"`), which is the repo root for
  > every profile. After a clean CLion rebuild all four `cmake-build-*` trees
  > recorded `CMAKE_HOME_DIRECTORY=/project`, none contained a
  > `compile_commands.json`, and each codemodel named project
  > `esp32_sensors_subprojects` with only the four aggregate custom targets —
  > all `UTILITY`, i.e. no compilable translation units. So all four profiles
  > build the same root aggregator, `-DIDF_TARGET=...` is unused by
  > `project(... NONE)`, and CLion gets no code model from them. Fixing it
  > needs either one CLion project per subproject or a merged
  > `compile_commands.json` exposed at the root. Until then
  > `scripts/dev.py build` is the way to build, and navigation/refactorings are
  > not driven by these profiles.

> Editing the image, mounts, or user? Change `docker-compose.yml` (and
> `Dockerfile`), **not** `devcontainer.json`. The Dev Container deliberately
> inherits everything from the compose service so the CLI and the IDE cannot
> drift apart.

#### Run configurations

Eight shared run configurations are committed under `.idea/runConfigurations/`
(and mirrored in the local `.idea/workspace.xml`). Six call `scripts/dev.py`,
so a single script is the source of truth for container commands — run them
from the CLion Run/Debug toolbar. The other two kinds are native
CLion **CMake** run configurations: the four below (one per subproject profile)
are meant to drive CLion's own CMake project model, which is what would make
the compile database live and therefore enable code navigation and
C++ **refactorings** — but see the "Verified reality" note above: they
currently all configure the root aggregator and produce no compile database, so
today they only build. `PROJECT_NAME` in each must match the root `project()`
name, `esp32_sensors_subprojects`, and `TARGET_NAME` must name a target **of
that root project** — i.e. one of the `*_build` aggregate targets, not a
subproject's ESP-IDF `<name>.elf`. The root is `project(... NONE)` and defines
only the aggregate custom targets, so an ELF name is unresolvable there and
CLion reports "cannot find target". CLion rewrites `PROJECT_NAME` to the real
root name once it has resolved the project, so a stale value there is a
symptom, not a fix; `ClionRunConfigurationTests` pins both invariants.

| Run configuration | Kind | What it does |
|---|---|---|
| esp32_sensors (container CMake) | CMake | Builds the `mcgowan_build` aggregate target via the `Espressif ESP32-S3` profile; intended (not yet achieved) to index the firmware for refactorings |
| Host tests (test, container CMake) | CMake | Builds the `test_build` aggregate target via the `Host tests (test)` profile |
| Host tests (espidf-cpp-test, container CMake) | CMake | Builds the `test-espidf-cpp_build` aggregate target via the `Host tests (espidf-cpp-test)` profile |
| Linux app (donny, container CMake) | CMake | Builds the `donny_build` aggregate target via the `Linux app (donny)` profile |
| Build firmware (container) | `scripts/dev.py build` | `idf.py -C mcgowan -B mcgowan/build_esp32s3 build` (and the other subprojects) |
| Flash & monitor (container) | `scripts/dev.py flash` | `idf.py -C mcgowan -B mcgowan/build_esp32s3 -p $ESP32_PORT flash monitor` (default `/dev/ttyUSB0`) |
| Host unit tests (container) | `scripts/dev.py test` | runs all five host test suites (below), `test-hal-common` first and `test-hal-stub` next; each suite also has a command that runs it alone |
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



### Flash and monitor

The host `/dev` tree is bind-mounted into the container. The board can be
plugged in at any time and it will be accessible by the container.

```bash
./scripts/dev.py flash
```

### Testing approach

There are two kinds of test suite, runnable from the host via `scripts/dev.py`:

- **Host-based unit tests** build for the ESP-IDF `linux` target with Unity —
  no hardware needed — in five projects:

  - `test-hal-stub/` — the pin-free `hal_stub` devices; what each test pins is
    described in `components/hal/AGENTS.md`.
  - `test-hal-common/` — the implementation-free `hal_common` vocabulary; what
    each test pins is described in `components/hal/AGENTS.md`.
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

Run the suites with `./scripts/dev.py test` (all five, stopping at the first
failure), or run one with `test-hal-common`, `test-hal-stub`, `test-logic`,
`test-espidf-cpp`, or `test-simulator`. See the `quality_checks` skill for the
gates and the fixing
policy.

### Quality checks

Run every quality gate through `scripts/dev.py` and fix what it reports. The
gates — clang-format, pycodestyle, clang-tidy, the build, the four host test
suites, and the `dev.py` self-check — their order, and the fixing policy are
documented in the `quality_checks` skill
(`.continue/skills/quality_checks/SKILL.md`). Quality gates are run via
`scripts/dev.py`; see the `quality_checks`, `cpp_qc`, and `python_qc` skills
for the required subsets.



### CMake

How the `CMakeLists.txt` files are written — including the rule that they
document the build and never the sources — is in the `cmake` skill
(`.continue/skills/cmake/SKILL.md`). Read it before editing a build file or
adding a source.

### Markdown

How the Markdown files are written — the line width, and the preference for
lists and subsections over sentences that run several items together — is in
the `markdown` skill (`.continue/skills/markdown/SKILL.md`). Read it before
writing or editing a `.md` file.


### Agent interaction

#### Committing

**Write the commit message to a temporary file, then commit with
`git commit -F <file>`.** Never pass it inline with `-m`, and never run a bare
`git commit`. Show the file to the user and wait for their approval before
committing: they may want to edit the message, and editing it afterwards needs
a rebase.

The message shape (subject ≤ 72 columns, imperative, no trailing period; a
blank line; a body wrapped at 72–79 columns that explains **why**; the Continue
trailer), why a file beats `-m`, and a worked example are in the `git` skill at
`.continue/skills/git/SKILL.md` — read it before writing a message.

## New tasks
- **Found a bug in that is unrelated to the current
  task? Do not fix it on the current branch.** Instead, park the work and fix
  it on its own branch so the fix can be reviewed and merged independently:
  1. **Stash** the in-progress changes with a message that describes the work
     done so far and what is still pending for the task, e.g.
     `git stash push -m "WIP <task>: done ...; pending ..."`.
  2. **Branch** from the base of the current branch (the commit the current
     branch was started from, not the current branch tip):
     `git switch -c fix/<short-name> <base-commit>`.
  3. **Fix** the problem, add tests where the bug is host-testable, then **pull
     and rebase** the current branch (`git pull --rebase`), format-check the
     staged files, commit, and **push** the branch.
  4. **Wait** to be told the fix has been merged. Do not continue as if the fix
     were available on the original branch.
  5. Once merged, **delete** the fix branch and re-sync the original branch
     (`git pull --rebase`) so it includes the merged fix.
  6. **Return** to the original branch and `git stash pop`; read the stash
     message to recall what was done and what was pending, then **resume** the
     original task.
- When the current work item is complete and no further input is expected,
  output `DONE!` as the final line of your response in the Continue (`cn`) CLI.
  Do **not** print `DONE!` if there are follow-up questions, pending sub-tasks,
  or work awaiting user confirmation.

  If more than five minutes has passed, and you have just outputted something 
  that requires a user response outputted DONE! then ring the bell
  five times.
