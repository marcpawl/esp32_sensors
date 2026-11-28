---
name: quality_checks
description: Run this repository's quality gates (clang-format, pycodestyle,
  clang-tidy, build, the seven host test suites, and the dev.py self-check) and
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
| Python style | `./scripts/dev.py pycodestyle` | PEP 8 with pycodestyle's defaults (79 columns). Every `*.py` file **and** every extensionless Python shebang script; `third_party/` is excluded. |
| Build | `./scripts/dev.py build [DIRECTORY...]` | Every discovered ESP-IDF project in/under the directories (from the repository root, all of them through the root aggregator). Fetches the pinned submodules first. Warnings are errors in this repository's own code (`cmake/compiler-warnings.cmake`); ESP-IDF and `third_party/` keep their upstream defaults. |
| GCC analyzer | `./scripts/dev.py build --analyzer [DIRECTORY...]` | Reconfigures the selected projects with `-fanalyzer` and replays only this repository's compile commands with `-S -o /dev/null`. It is a static check: no objects are written and nothing is linked. Findings are errors under `-Werror`. |
| Static analysis | `./scripts/dev.py clang-tidy [<DIRECTORY>\|<FILE>]... [--fix]` | The repository's `.clang-tidy` check set plus clang-tidy's defaults (`clang-diagnostic-*`, `clang-analyzer-*`) over the named files and every source under the named directories (default: the current directory); a directory that is itself an ESP-IDF project uses that project's compile database, any other draws from every database below it. A named header borrows a matching translation unit's flags. Quoted globs are expanded by `dev.py`. `--fix` applies the suggested fixes in place (re-run `format` afterwards). **Reads the compile databases; it never builds them**, so `build` must have run first. Vendored code is excluded. |
| Formatting (apply) | `./scripts/dev.py format [<DIRECTORY>\|<FILE>]...` | clang-format over the named files and the tracked **and untracked** C/C++ sources under the named directories (default: the current directory; from the repository root that is `mcgowan/` and `components/`). Quoted globs are expanded by `dev.py`. |
| Formatting (check) | `./scripts/dev.py format-check [<DIRECTORY>\|<FILE>]...` | Same file set as `format`, fails instead of editing. |
| Formatting (staged) | `./scripts/dev.py format-check-staged [<DIRECTORY>\|<FILE>]...` | clang-format over the **staged** content of the named files or directories, honouring nested `.clang-format` / `.clang-format-ignore` files. This is the hook's variant. |
| Tests | `./scripts/dev.py test-linux [DIRECTORY...]` | Every discovered host (`linux`) suite, in platform-layer order (or only those in/under the directories; default: the current directory). Stops at the first failure. |
| On-target tests | `./scripts/dev.py test-esp32 [DIRECTORY...]` | Every discovered non-`linux` test project under a `tests/` directory is built, flashed, and run through its `test.py` driver. Needs the board and `ESP32_PORT`; not part of the CI gates. |
| Harness self-check | `./scripts/dev.py self-check` | Stdlib unit tests for `scripts/dev.py` itself. **Host-only**: it refuses to run in the container (the host-side decisions it pins never happen there, and the container has no Docker daemon). It needs Docker and the pulled image, because a few probes run the pinned image's shells; without either, those probes fail and name the pull command. CI runs it on the runner. |

Run one suite with its directory, e.g.
`./scripts/dev.py test-linux components/utility/tests/test-utility`; there are
no per-suite commands.

## Order to run them

Cheapest first:

1. `./scripts/dev.py pycodestyle`
2. `./scripts/dev.py format-check-staged` (or `format-check` for the whole
   tree)
3. `./scripts/dev.py build`
4. `./scripts/dev.py build --analyzer`
5. `./scripts/dev.py clang-tidy`
6. `./scripts/dev.py test-linux`

CI (`build-and-test` in `.github/workflows/ci.yml`) adds
`./scripts/dev.py self-check` before them and runs `format-check` (not the
staged variant) at the end. It runs the analyzer as a separate static gate,
then `clean`, then a plain `build` immediately before `test-linux` so the
suites execute with UBSan and ASan. The clean makes the switch explicit;
`dev.py` also passes the cache variable in both directions, so no stale
analyzer flags survive.

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
  is not ready. New host-testable logic belongs in
  `components/logic/liblogic` (headers under `include/logic/`, `.cpp` files in
  its `SRCS`); firmware-only sources must be added to both
  `mcgowan/main/CMakeLists.txt` and
  `components/logic/tests/test-logic/main/CMakeLists.txt`.
- **`third_party/` is off limits:** it is outside the formatted roots and
  excluded from clang-tidy, so vendored code stays byte-identical to upstream.

## Do not bypass the gates

The hook is the point of the gates. `git commit --no-verify` is used only when
the user explicitly asks for it. Commit messages are written to a file and
passed with `git commit -F` — see the `git` skill.
