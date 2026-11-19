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

## Vendored code

`third_party/` is copied verbatim from upstream and is never edited — its
`CMakeLists.txt` files follow upstream's conventions, not this rule.
