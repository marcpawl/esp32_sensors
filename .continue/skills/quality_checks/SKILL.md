---
name: quality_checks
description: Run this repository's quality gates (clang-format, pycodestyle,
  clang-tidy, build, the four host test suites, and the dev.py self-check) and
  fix what they report. Use before committing, before opening or updating a
  pull request, or whenever asked to verify quality.
---

# Quality checks in this repository

Run every check through `scripts/dev.py` from the repository root. It is the
single source of truth for container tasks: it re-execs the command in the
pinned dev image, so the host needs only Docker. Do not reproduce these
commands with ad-hoc `docker compose run`.

## The gates

| Gate | Command | What it checks |
|---|---|---|
| Python style | `./scripts/dev.py pycodestyle` | PEP 8 with pycodestyle's defaults (79 columns). Every `*.py` file **and** every extensionless Python shebang script (e.g. `.githooks/pre-commit`); `third_party/` is excluded. |
| Build | `./scripts/dev.py build [SUBPROJECT...]` | All subprojects through the root aggregator, or just the named ones (`mcgowan`, `test-hal-stub`, `test-hal-common`, `test-logic`, `test-espidf-cpp`, `donny`). Fetches the pinned submodules first. |
| Static analysis | `./scripts/dev.py clang-tidy [PROJECT...]` | clang-tidy's default checks (`clang-diagnostic-*`, `clang-analyzer-*`) over every subproject, with `--warnings-as-errors=*`. **Reads the compile databases; it never builds them**, so `build` must have run first. Vendored code is excluded. |
| Formatting (apply) | `./scripts/dev.py format` | clang-format over the tracked **and untracked** C/C++ sources under `mcgowan/`, `components/`, `test-hal-stub/`, `test-hal-common/`, `test-logic/`, `test-espidf-cpp/`, and `donny/`. |
| Formatting (check) | `./scripts/dev.py format-check` | Same file set as `format`, fails instead of editing. |
| Formatting (staged) | `./scripts/dev.py format-check-staged` | clang-format over the **staged** content only, honouring nested `.clang-format` / `.clang-format-ignore` files. This is the hook's variant. |
| Tests | `./scripts/dev.py test` | Every host suite, in order: `test-hal-common`, `test-hal-stub`, `test-logic`, `test-espidf-cpp`. Stops at the first failure. |
| Harness self-check | `./scripts/dev.py self-check` | Stdlib unit tests for `scripts/dev.py` itself. **Host-only**: it refuses to run in the container (the host-side decisions it pins never happen there). CI runs it on the runner. |

Individual host suites are also available as `test-hal-common`,
`test-hal-stub`, `test-logic`, and `test-espidf-cpp`.

## Order to run them

Cheapest first, matching `.githooks/pre-commit`:

1. `./scripts/dev.py pycodestyle`
2. `./scripts/dev.py build`
3. `./scripts/dev.py clang-tidy`
4. `./scripts/dev.py format-check-staged` (or `format-check` for the whole tree)
5. `./scripts/dev.py test`

The pre-commit hook runs exactly those five gates and rejects the commit on the
first failure. CI (`build-and-test` in `.github/workflows/ci.yml`) adds
`./scripts/dev.py self-check` before them and runs `format-check` (not the
staged variant) at the end.

`format` is the exception to the ordering: run it **before** the checks when
sources changed, so `format-check` verifies rather than reports.

## Fixing what a gate reports

- **Formatting:** run `./scripts/dev.py format`, then re-check. Do not
  hand-format; the committed `.clang-format` (Google base, 4-space indent, 80
  columns, `Standard: c++20`) is the authority. Nested style files are honoured
  because clang-format searches upward from each source.
- **pycodestyle:** fix the source. No `--max-line-length` or other option is
  passed, so the gate and a bare `pycodestyle` run agree; the repository
  conforms to the tool, not the other way round.
- **clang-tidy:** fix the finding. A `NOLINT` is acceptable only when the
  analyzer is right but the code must stay as it is, and then it carries a
  comment explaining why. Never edit an ESP-IDF header under `$IDF_PATH` to
  make a check pass — that tree is read-only and the edit would not reproduce
  in CI.
- **Build or test failure:** fix the code; a red build or suite means the work
  is not ready. New host-testable logic belongs in `components/logic` (headers
  under `include/logic/`, `.cpp` files in its `SRCS`); firmware-only sources
  must be added to both `mcgowan/main/CMakeLists.txt` and
  `test-logic/main/CMakeLists.txt`.
- **`third_party/` is off limits:** it is outside the formatted roots and
  excluded from clang-tidy, so vendored code stays byte-identical to upstream.

## Do not bypass the gates

The hook is the point of the gates. `git commit --no-verify` is used only when
the user explicitly asks for it. Commit messages are written to a file and
passed with `git commit -F` — see the `git` skill.
