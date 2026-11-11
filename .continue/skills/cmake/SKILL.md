---
name: cmake
description: How the CMakeLists.txt files in this repository are written. Use
  before editing any CMakeLists.txt, or when adding or moving a source file.
---

# CMakeLists.txt

## The rule

**A `CMakeLists.txt` documents the build, never the code.** Do not list the
source files in a comment and do not say what they contain: no per-file
guide, no "this component provides ..." inventory, no summary of the modules
that live in the directory.

The source is the documentation. A reader who needs to know what a file does
opens it, and a comment that answers that question for them is a second copy
of the answer that drifts the moment the code changes.

```cmake
# BAD: describes the sources.
#   temp_format.cpp   Centred-hundredths temperature formatting.
#   payload.cpp       The §9 upload payload builder.

# GOOD: describes the build.
# `esp_timer` is a public requirement because `hal/timer_board.hpp` names
# `esp_timer_handle_t` in the handle class.
```

## What a build comment explains

Comments explain *why* the build is configured the way it is:

- an unusual flag or include directory, and the constraint that forces it;
- a `REQUIRES` / `PRIV_REQUIRES` entry that is not self-evident;
- a deliberate exclusion (`EXCLUDE_COMPONENTS`), an ordering constraint
  (`must be set before project()`), or a `WHOLE_ARCHIVE` need;
- a failure guard, such as the `DOCKER_IMAGE` check that refuses a host
  build;
- how to build or run the project.

They never explain what the code does.

## Adding a source

A new `.cpp` file has to be listed wherever its target collects sources —
`SRCS` in a component, `idf_component_register()` in a `main` component.
That is build configuration, not documentation, so it belongs in the file;
the rule above only forbids describing the sources, not naming them as
inputs.

## Vendored code

`third_party/` is copied verbatim from upstream and is never edited — its
`CMakeLists.txt` files follow upstream's conventions, not this rule.
