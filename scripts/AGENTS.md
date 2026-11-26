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
| host | `up` | Start the `dev` service with `docker compose up -d` |
| host | `down` | Stop/remove the dev containers with `docker compose down --remove-orphans` |
| both | `submodules` | Fetch the pinned git submodules (`third_party/sml`) |
| both | `build [--analyzer[=BOOL]] [DIRECTORY...]` | Build every project in/under the directories (from the root, the root aggregator builds everything); the analyzer is off by default, `--analyzer` enables it |
| both | `build-test-espidf-cpp-linux` | Build the `libespidfcpp` unit tests for `linux` |
| both | `test-linux [DIRECTORY...]` | Build and run every discovered host (`linux`) suite in path order, or those in/under the directories |
| both | `test-esp32 [DIRECTORY...]` | Build, flash, and run every discovered on-target test project through its `test.py` driver |
| both | `self-check` | Run the host unit tests for `dev.py` itself (host only; its image probes need Docker and the image fetched by `pull`, and fail without them) |
| both | `flash` | `idf.py flash monitor` |
| both | `monitor` | `idf.py monitor` |
| both | `menuconfig` | `idf.py menuconfig` |
| both | `format [<DIRECTORY>\|<FILE>]...` | clang-format the named C/C++ files and every source under the named directories (tracked and untracked); default: the current directory |
| both | `format-check [<DIRECTORY>\|<FILE>]...` | Non-zero exit if any such source is unformatted |
| both | `format-check-staged [<DIRECTORY>\|<FILE>]...` | Same, for the staged content of those scopes only |
| both | `pycodestyle` | PEP 8 (79 columns) over all Python sources |
| both | `clang-tidy [<DIRECTORY>\|<FILE>]... [--fix]` | Static analysis over the named files and every source under the named directories (default: the current directory; a project directory uses its own compile database, any other draws from all databases below it); `run-clang-tidy` shards the rewritten database across CPUs and may interleave output; `--fix` applies fixes in place serially |
| both | `shell [CMD...]` | Run a command in the container; no arguments = login shell |
| host | `ssh [CMD...]` | SSH into the running dev container; no arguments = interactive login shell |
| host | `clion-archive [ARCHIVE]` | Copy a local `CLion-*.tar.gz` into the container's Toolbox download cache |
| both | `clion-compdb` | Merge every subproject's `compile_commands.json` into `.clion/compile_commands.json` for CLion's code model |
| both | `clion-run-configs` | Generate CLion CMake profiles and linux ELF run/debug configurations for every discovered linux executable project |
| host | `clion-manual-test` | Start CLion and prompt through the manual tooltip check; refuses in CI or when CLion is already running |
| both | `complete` | Internal: list subcommands matching a prefix |
| host | `install-hooks` | Install the git commit hooks |

Set `ESP32_PORT` (e.g. `/dev/ttyACM0`) to target another serial device.

### Quoting arguments to `shell`

`dev.py shell` joins its arguments with spaces and runs the result through
`bash -c`; it does not quote them. An argument containing spaces therefore
splits into several words, which a command such as `gh pr create --title`
reports as `unknown arguments`. Quote the whole command as one argument:

```bash
./scripts/dev.py shell "gh pr create --title 'A multi-word title' --body-file .git/pr.md"
```

Prefer a `--body-file`/`-F` flag over inline text for anything long, and put
the file inside the repository: the container cannot see the host's `/tmp`.

`ssh` is host-only: it starts (or reuses) the long-running compose container
with `docker compose up -d dev` and connects over the loopback-published sshd.
It authenticates with the host's `~/.ssh` keys, which the container already
mounts read-only, and runs the command through a login shell in `/project`.
`DEV_SSH_PORT` moves the host-side port (default 2222).

`up` is host-only. It runs `docker compose up -d` for the `dev` service using
the repository's compose file, so the container is already running before
`ssh`, a Dev Container, or a CLion backend connects. `down` is its host-only
counterpart: it runs `docker compose down --remove-orphans`, removing the
container and its in-memory state such as the Toolbox logs.

`clion-archive` is host-only too. It copies a local `CLion-*.tar.gz` into the
container path the Toolbox agent downloads the remote backend to, so CLion
installs from that file instead of fetching it. With no argument it uses the
only `~/Downloads/CLion-*.tar.gz`. It stops the Toolbox agent first, clears an
interrupted download's `.tmp`/`journal`, and scp's the archive as a `.part`
file that is renamed only after its SHA-256 matches.

### `clion-compdb`

The root CMake project is an aggregator with no C/C++ code model. This command
merges the databases written by `build` into `.clion/compile_commands.json`,
which CLion opens as one compilation-database project covering every source.
The file is generated, git-ignored, and safe to delete.

### `clion-run-configs`

Discovers every `linux`-target ESP-IDF project (host suites and applications
such as `donny`) and (re)generates the shared linux ELF run/debug
configurations in `.idea/runConfigurations/` (`Linux_ELF__*__container.xml`).
Each configuration builds through the matching root `<subproject>_build`
target, runs the ELF from `build_linux`, and selects the corresponding
`.idea/cmake.xml` profile, so it can be run and debugged from CLion. The
linux profiles in `.idea/cmake.xml` are updated to match the discovered
projects. Other shared run configurations are left untouched; stale generated
linux ELF configs are removed. Re-run it after adding or renaming a linux
executable project.

Databases are read in a fixed order:

- `mcgowan` (Xtensa firmware)
- the other chip projects, e.g. the `esp_timer` board suite
- the `linux` suites and application, in path order

The first database that lists a source wins, so a file built for several
targets keeps the ESP-IDF include paths and defines; host-only test files come
from their `linux` project. Command lines are copied unchanged, including
their container-absolute `/project/...` and `/opt/esp/...` paths.

Run `build` first: a project without a database is skipped with a warning.
After a rebuild, run `clion-compdb` again and reload the project in CLion.
See the root `AGENTS.md` for opening the database as a CLion project.

CLion's shared project files for that database are committed under
`.clion/.idea/`: `.gitignore`, `editor.xml`, `misc.xml`, and `vcs.xml`.
`misc.xml` maps the content root to `/project`, so opening
`/project/.clion/compile_commands.json` from a fresh clone does not require
*Change Project Root*. The generated database and `workspace.xml` stay local.

### `clion-manual-test`

This host-only wizard records a manual check of the tooltip workflow. Refuse
to run it in CI (`CI` set) or inside the container: it needs the host's
JetBrains Toolbox launcher and a terminal for prompts.

The launcher is chosen in this order:

- `CLION_LAUNCHER`, which may include arguments
- `clion` on `PATH`
- the Toolbox `scripts/clion`, `scripts/clion1`, and `apps/clion/bin/clion`
  paths under the host user's home directory

The wizard's first two actions are fixed:

1. Refuse and abort when CLion is already running on the host. It scans
   `/proc/<pid>/cmdline` for CLion's install path and skips its own process,
   so the `clion-manual-test` argument is not a false positive. There is no
   check inside the dev container, which is about to be removed.
2. Kill and delete the dev container with `dev.py down`
   (`docker compose down --remove-orphans`). A failure here aborts the test.

The command then runs `dev.py up`, `dev.py build`, `dev.py clion-compdb`, and
`dev.py clion-run-configs` in that order to start the container and prepare
the compilation database and linux ELF run/debug configurations. If
`~/Downloads/CLion-*.tar.gz` exists, it calls `dev.py clion-archive` to seed
the backend archive; the first manual step then requires Toolbox log evidence
that the archive was reused from the local download cache. A "will be reused
as a cached previous download" line or a "Correct checksum for <cache path>"
line passes even when the reuse line also names the remote URL; a remote
download line fails. Without an archive, that step tells the user to wait for
*Downloading IDE backend...* to finish. CLion is started without blocking.

Before the first prompt the wizard polls the Toolbox log for the opened remote
project. No `serverMode` entry yet means CLion has not connected, so it polls
every 10 seconds for up to 10 minutes before asking
`No project opened in 10 minutes; wait some more?`:

- `y` — wait another 10 minutes
- `n` — fail with `FAILED: no project was opened in 10 minutes.`
- EOF — abort the run

A `serverMode` entry other than `/project` fails at once with
`FAILED: the wrong project is opened in CLion.` without waiting or prompting.

The wizard prints three manual steps and asks for `y` or `n` after each one:

1. Connect CLion to the remote project.
2. Verify the ESP-IDF tooltip, directly after the connection.
3. Run the `test-espidf-cpp-linux` linux ELF run/debug configuration.

The compilation-database checks are the tooltip step's fallback, numbered
2.1, 2.2, and 2.3: open the database, check path settings, and check the
toolchain. They run in order only when step 2 fails and are skipped when it
succeeds; the run still fails on step 2 after they finish.

Before the tooltip and run steps it scans the Toolbox log and fails if the
remote project directory is not `/project`, so a wrong connection directory is
caught. An invalid answer is rejected, EOF aborts the run, and `n` fails the
run and names the step. Only the three default steps, plus the backend log
check when an archive was seeded and the remote project directory checks,
produce a pass. The logic lives in `scripts/clion_manual_test.py`, with all
process starts and input injected so `self-check` can cover it without
launching an IDE. Running that file directly also works; it delegates to the
same `dev.py` command.

`build`, `test-linux`, `test-esp32`, and `clang-tidy` fetch missing submodules
first.

`build` and the `linux` test commands run `idf.py set-target` only when a
project's build tree is new or records a different target. `set-target`
deletes ninja's dependency log, so running it before every build recompiles
every translation unit; a second, unchanged build now does no work.

`format`, `format-check`, `format-check-staged`, and `clang-tidy` take
directory or file arguments:

- A directory selects every C/C++ source under it.
- A file selects only that source; `clang-tidy` also accepts headers, which
  borrow the compile flags of their matching translation unit.
- A glob such as `*/nvs.*` expands to files or directories. The shell may
  expand it, or it may be quoted to let `dev.py` expand it.

Several directories and files may be named in one command; host absolute
paths and `/project/...` paths work too. With no argument, these commands
start from the current directory. An explicit file must be a C/C++ source;
an explicit scope inside a vendored tree is rejected. Glob matches that are
not sources, or that are vendored, are ignored. A scope outside the
repository, or a caller outside it, is rejected.

`build`, `test-linux`, and `test-esp32` still take directories only: an
individual `.cpp` is not a build target, so compiling one source to a `.o`
would not produce a usable firmware or library.

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
directory and file scoping for build/test-linux/test-esp32/format/tidy, the
staged format check, the run-configuration invariants, the test commands,
clang-tidy wiring, the `sml` submodule pin, the `clion-compdb` merge order
and atomic write, that `dev.py` is on the PATH of the pinned image for every
kind of shell, and that `self-check` is host-only. Add coverage there when
changing `dev.py`.

## Shell completion

`scripts/dev.py-completion.bash` provides bash tab-completion for the
subcommands, driven by `dev.py complete <prefix>`.

## `scripts/idf.sh`

Establishes the ESP-IDF environment, sourcing `$IDF_PATH/export.sh` when
needed. The root aggregator invokes `idf.py` through it (`set(IDF_RUNNER ...)`)
so an IDE-launched build works.
