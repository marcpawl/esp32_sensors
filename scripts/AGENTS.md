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
| both | `build [--analyzer[=BOOL]] [DIRECTORY...]` | Build every project in/under the directories (from the root, the root aggregator builds everything); the analyzer is off by default, `--analyzer` enables it |
| both | `build-test-espidf-cpp` | Build the `libespidfcpp` unit tests for `linux` |
| both | `test-linux [DIRECTORY...]` | Build and run every discovered host (`linux`) suite in path order, or those in/under the directories |
| both | `test-esp32 [DIRECTORY...]` | Build, flash, and run every discovered on-target test project through its `test.py` driver |
| both | `self-check` | Run the host unit tests for `dev.py` itself (host only; its image probes need Docker and the image fetched by `pull`, and fail without them) |
| both | `flash` | `idf.py flash monitor` |
| both | `monitor` | `idf.py monitor` |
| both | `menuconfig` | `idf.py menuconfig` |
| both | `format [DIRECTORY...]` | clang-format the C/C++ sources in or under the directories (tracked and untracked); default: the current directory |
| both | `format-check [DIRECTORY...]` | Non-zero exit if any such source is unformatted |
| both | `format-check-staged [DIRECTORY...]` | Same, for the staged content of those directories only |
| both | `pycodestyle` | PEP 8 (79 columns) over all Python sources |
| both | `clang-tidy [DIRECTORY...] [--fix]` | Static analysis over every source under the directories (default: the current directory; a project directory uses its own compile database, any other draws from all databases below it); `--fix` applies fixes in place |
| both | `shell [CMD...]` | Run a command in the container; no arguments = login shell |
| host | `ssh [CMD...]` | SSH into the running dev container; no arguments = interactive login shell |
| both | `complete` | Internal: list subcommands matching a prefix |
| host | `install-hooks` | Install the git commit hooks |

Set `ESP32_PORT` (e.g. `/dev/ttyACM0`) to target another serial device.

`ssh` is host-only: it starts (or reuses) the long-running compose container
with `docker compose up -d dev` and connects over the loopback-published sshd.
It authenticates with the host's `~/.ssh` keys, which the container already
mounts read-only, and runs the command through a login shell in `/project`.
`DEV_SSH_PORT` moves the host-side port (default 2222).

`build`, `test-linux`, `test-esp32`, and `clang-tidy` fetch missing submodules
first.

`format`, `format-check`, `format-check-staged`, `build`, `test-linux`,
`test-esp32`, and `clang-tidy` all take directory arguments (a host absolute
path or `/project/...` works too) and start from the current directory when
given none, so a subset can be worked on from inside it. A directory outside
the repository, or a caller outside it, is rejected; `third_party/` is never
formatted or checked.

`build` and the test commands discover ESP-IDF projects on disk: a project is
a directory whose `CMakeLists.txt` includes ESP-IDF's `project.cmake`, so a new
project or suite needs no entry in `dev.py`. A directory argument covers every
project in or under it; there are no subproject names, so `build mcgowan`
means the `mcgowan/` directory relative to the current directory. A
`test-linux` suite is a `linux`-target project under a `tests/` directory;
`test-esp32` runs the non-`linux` ones through their `test.py` driver.

## `scripts/test_dev.py`

Stdlib-`unittest` tests for `dev.py` itself, run by
`scripts/dev.py self-check`. It pins the format/pycodestyle source discovery,
directory scoping for build/test-linux/test-esp32/format/tidy, the staged
format check, the run-configuration invariants, the test commands, clang-tidy
wiring, the `sml` submodule pin, that `dev.py` is on the PATH of the pinned
image for every kind of shell, and that `self-check` is host-only. Add
coverage there when changing `dev.py`.

## Shell completion

`scripts/dev.py-completion.bash` provides bash tab-completion for the
subcommands, driven by `dev.py complete <prefix>`.

## `scripts/idf.sh`

Establishes the ESP-IDF environment, sourcing `$IDF_PATH/export.sh` when
needed. The root aggregator invokes `idf.py` through it (`set(IDF_RUNNER ...)`)
so an IDE-launched build works.
