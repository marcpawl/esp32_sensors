# The scripts

`scripts/` holds the container task runner and its supporting files.
Repository-wide guidance is in the root `AGENTS.md`.

## `scripts/dev.py`

`dev.py` can be executed on the host or in the container. Command format:

```bash
./scripts/dev.py COMMAND
```

Platform is either `host` or `both`. Host-only commands refuse to run in the
container; the rest create and run the same command in the container when
invoked from the host.

| Platform | Command | Action |
|---|---|---|
| host | `pull` | Pull the pinned prebuilt dev image |
| host | `image` | Build the dev image locally from the `Dockerfile` |
| both | `submodules` | Fetch the pinned git submodules (`third_party/sml`) |
| both | `build [SUBPROJECT...]` | Build all subprojects via the root aggregator, or just the named ones |
| both | `build-test-espidf-cpp` | Build the `libespidfcpp` unit tests for `linux` |
| both | `test` | Run all host suites in order |
| both | `test-hal-stub` | Build and run the `test-hal-stub/` suite |
| both | `test-hal-common` | Build and run the `test-hal-common/` suite |
| both | `test-logic` | Build and run the `test-logic/` suite |
| both | `test-espidf-cpp` | Build and run the `test-espidf-cpp/` suite |
| both | `test-simulator` | Build and run the `test-simulator/` suite |
| both | `self-check` | Run the host unit tests for `dev.py` itself (host only) |
| both | `flash` | `idf.py flash monitor` |
| both | `monitor` | `idf.py monitor` |
| both | `menuconfig` | `idf.py menuconfig` |
| both | `format` | clang-format all sources (tracked and untracked) |
| both | `format-check` | Non-zero exit if anything is unformatted |
| both | `format-check-staged` | Same, for staged code only |
| both | `pycodestyle` | PEP 8 (79 columns) over all Python sources |
| both | `clang-tidy [PROJECT...]` | Static analysis over built projects |
| both | `shell [CMD...]` | Run a command in the container; no arguments = login shell |
| both | `cn` | Run the Continue CLI in the container |
| both | `complete` | Internal: list subcommands matching a prefix |
| host | `install-hooks` | Install the git commit hooks |

Set `ESP32_PORT` (e.g. `/dev/ttyACM0`) to target another serial device.

`build`, `test`, and `clang-tidy` fetch missing submodules first.

## `scripts/test_dev.py`

Stdlib-`unittest` tests for `dev.py` itself, run by
`scripts/dev.py self-check`. It pins the format/pycodestyle source discovery,
the staged format check, the run-configuration invariants, the host-test
commands, clang-tidy wiring, the `sml` submodule pin, `dev.py cn`, and that
`self-check` is host-only. Add coverage there when changing `dev.py`.

## Shell completion

`scripts/dev.py-completion.bash` provides bash tab-completion for the
subcommands, driven by `dev.py complete <prefix>`.

## `scripts/idf.sh`

Establishes the ESP-IDF environment, sourcing `$IDF_PATH/export.sh` when
needed. The root aggregator invokes `idf.py` through it (`set(IDF_RUNNER ...)`)
so an IDE-launched build works.
