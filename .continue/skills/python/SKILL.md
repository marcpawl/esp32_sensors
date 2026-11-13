---
name: python
description: How Python sources in this repository are written and checked. Use before writing or editing any Python — `scripts/dev.py`, `scripts/test_dev.py`, or a shebang script — or when deciding which quality gates to run before committing.
---

# Python

## Quality gates before committing

The automatic pre-commit hook has been removed (issue #66); the gates run on
GitHub instead. Run the relevant subset locally depending on what changed:

- **A C++ file or `CMakeLists.txt` change:**
  1. `./scripts/dev.py build`
  2. `./scripts/dev.py format`
  3. `./scripts/dev.py build` again (only if `format` changed anything)
  4. `./scripts/dev.py test`
  5. `./scripts/dev.py clang-tidy`
- **A Python file change:**
  1. `./scripts/dev.py pycodestyle`
  2. `./scripts/dev.py self-check`
- **A change strictly to `.md` files:** no tests are required.

Run the gates that match the files you touched; mixing C++ and Python changes
means running both sets.

## Style

Python sources are checked with `pycodestyle` (PEP 8, 79 columns) over every
Python file in the repository — `*.py` plus extensionless Python shebang
scripts (such as `.githooks/pre-commit` historically). `third_party/` is
excluded. `scripts/dev.py pycodestyle` runs `python3 -m pycodestyle` with no
options of its own, so a bare `pycodestyle` agrees with it.

## The dev.py self-check

`scripts/dev.py self-check` runs `scripts/test_dev.py` (stdlib `unittest`) on
the host. It is the test suite for `dev.py` itself and must pass whenever
`dev.py` changes. Run it after editing `scripts/dev.py` or
`scripts/test_dev.py`.

## Conventions

- Use `python3` and the standard library where possible; `dev.py` and its tests
  are stdlib-only.
- Keep `scripts/dev.py` the single source of truth for container commands; add
  new subcommands there rather than ad-hoc shell.
- Extensionless Python scripts must carry a `#!/usr/bin/env python3` (or
  equivalent) shebang so `pycodestyle` discovery picks them up.
