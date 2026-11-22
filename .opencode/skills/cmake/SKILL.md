---
name: cmake
description: How the CMakeLists.txt files in this repository are written. Use
  before editing any CMakeLists.txt, or when adding or moving a source file.
---

# CMakeLists.txt

## The rule

**A `CMakeLists.txt` documents the build, never the code.** Do not list the
source files in a comment and do not say what they contain: no per-file guide,
no "this component provides ..." inventory, no summary of the modules that live
in the directory.

The source is the documentation. A reader who needs to know what a file does
opens it, and a comment that answers that question for them is a second copy of
the answer that drifts the moment the code changes.

```cmake
# BAD: describes the sources.
#   temp_format.cpp   Centred-hundredths temperature formatting.
#   payload.cpp       The §9 upload payload builder.

# GOOD: describes the build.
# `esp_hw_support` is a public requirement because `board.cpp` calls
# `esp_sleep_enable_timer_wakeup()` / `esp_deep_sleep_start()`.
```

## What a build comment explains

Comments explain *why* the build is configured the way it is:

- an unusual flag or include directory, and the constraint that forces it;
- a `REQUIRES` / `PRIV_REQUIRES` entry that is not self-evident;
- a deliberate exclusion (`EXCLUDE_COMPONENTS`), an ordering constraint
  (`must be set before project()`), or a `WHOLE_ARCHIVE` need;
- a failure guard, such as the container-marker check that refuses a host
  build;
- how to build or run the project.

They never explain what the code does.

## Warning-free configures

Every project must configure without CMake warnings, with one exception. A
project that links `esp_wifi` — today only `mcgowan` — prints five warnings
from ESP-IDF's `component_validation.cmake` on every configure. Each names
`esp_wifi` or `wpa_supplicant` and reports an include directory that "belongs
to component ... but is being used by component ...". They are not this
repository's configuration: ESP-IDF registers those two components with each
other's private include directories (see espressif/esp-idf#17840,
IDFGH-16756). Espressif's hotfix is on its v6.0 branch only, and the pinned
`espressif/idf:release-v6.1` base image does not carry it.

- Treat a build as warning-free when those five are the only `CMake Warning`
  lines; any other warning is ours to fix.
- Check a project with `idf.py -C <project> -B <build dir> reconfigure` and
  grep the output for `CMake Warning`. A project that does not link `esp_wifi`
  must report none.
- Delete this section once the base image in `docker/Dockerfile` carries the
  fix.

Keep a project's `COMPONENTS` list as small as its `main` needs. Left
unrestricted it builds every default IDF component, which is slow and pulls
in `esp_wifi` even for a test that never touches Wi-Fi — the on-target
`esp_timer` tests did exactly that.

## Adding a source

A new `.cpp` file has to be listed wherever its target collects sources —
`SRCS` in a component, `idf_component_register()` in a `main` component. That
is build configuration, not documentation, so it belongs in the file; the rule
above only forbids describing the sources, not naming them as inputs.

After a successful build that verifies adding or removing a `.cpp` or `.hpp`
in a `CMakeLists.txt`, run `./scripts/dev.py clion-compdb` to refresh
`.clion/compile_commands.json` for CLion's code model.

## Adding a project

Adding a new ESP-IDF subproject means adding its `CMakeLists.txt` and wiring
it into the build (for example, the root aggregator's `_idf_subproject(...)`
call when it belongs there).

- Run `./scripts/dev.py clion-run-configs` after adding a new project so
  CLion's linux ELF run/debug configurations and CMake profiles stay in sync
  with the discovered projects.

## Adding to a project
When a C++ file (.cpp, .hpp) is added or removed from a CMakeLists.txt

- After a successful build, run `./scripts/dev.py clion-compdb` to refresh the
  merged compilation database for CLion's code model.

## Headers are target sources

**Every header the target owns must be listed in `target_sources()`**, even
though CMake needs no such thing to build. A CMake file-API client — CLion —
attaches a header to a target only when the target names it or a compiled
source includes it, so an unreferenced or newly added header otherwise gets no
code model ("this file does not belong to any project target"). Listing a
header adds no compile step: CMake treats a header as a header.

- List every header under the target's `INCLUDE_DIRS`, in path order, as a
  path relative to the `CMakeLists.txt` (or through the component's own path
  variable, e.g. `${mcgowan_src}/include/...`).
- Put the call *after* `idf_component_register()`: that call is what defines
  `${COMPONENT_LIB}` in the component's scope.
- Use `PRIVATE`; the headers are already exported through `INCLUDE_DIRS`.
- A header-only target (`components/sml`) and vendored trees are the
  exceptions: their headers belong to upstream, so they are exposed through
  `INCLUDE_DIRS` only.
- Add a new header to the list in the same change that adds the file.

```cmake
# Headers are target sources so a CMake file-API client (CLion) attaches them
# to this target; CMake adds no compile step for a header.
target_sources(${COMPONENT_LIB} PRIVATE
    "include/logic/mode.hpp"
    "include/logic/payload.hpp"
)
```

## Vendored code

`third_party/` is copied verbatim from upstream and is never edited — its
`CMakeLists.txt` files follow upstream's conventions, not this rule.
