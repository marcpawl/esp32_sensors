---
name: C++ quality checks
description: How C++ and CMake changes are checked
---

# Python

## Quality gates before committing

Changes to a C++ or CMake file should pass:

  1. `./scripts/dev.py build`
  2. `./scripts/dev.py format`
  3. `./scripts/dev.py build` again (only if `format` changed anything)
  4. `./scripts/dev.py test`
  5. `./scripts/dev.py clang-tidy`

Run the checks before pushing.

