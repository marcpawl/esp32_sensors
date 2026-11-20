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
| host | `clion-archive [ARCHIVE]` | Copy a local `CLion-*.tar.gz` into the container's Toolbox download cache |
| both | `clion-compdb` | Merge every subproject's `compile_commands.json` into `.clion/compile_commands.json` for CLion's code model |
| host | `clion-manual-test` | Start CLion and prompt through the manual tooltip check; refuses in CI |
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

The command checks for `.clion/compile_commands.json`, then calls
`dev.py down` and `dev.py up`. If `~/Downloads/CLion-*.tar.gz` exists, it calls
`dev.py clion-archive` to seed the backend archive; the first manual step
then requires Toolbox log evidence that the archive was reused from the
local download cache. A "will be reused as a cached previous download" line
or a "Correct checksum for <cache path>" line passes even when the reuse
line also names the remote URL; a remote download line fails. Without an
archive, that step tells the user to wait for *Downloading IDE backend...*
to finish. CLion is started without blocking.

The wizard prints five manual steps and asks for `y` or `n` after each one.
An invalid answer is rejected, EOF aborts the run, and `n` fails the run and
names the step. Only five confirmed steps, plus the backend log check when
an archive was seeded, produce a pass. The logic lives in
`scripts/clion_manual_test.py`, with all process starts and input injected so
`self-check` can cover it without launching an IDE. Running that file
directly also works; it delegates to the same `dev.py` command.

`build`, `test-linux`, `test-esp32`, and `clang-tidy` fetch missing submodules
first.

`build` and the `linux` test commands run `idf.py set-target` only when a
project's build tree is new or records a different target. `set-target`
deletes ninja's dependency log, so running it before every build recompiles
every translation unit; a second, unchanged build now does no work.

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
wiring, the `sml` submodule pin, the `clion-compdb` merge order and atomic
write, that `dev.py` is on the PATH of the pinned image for every kind of
shell, and that `self-check` is host-only. Add coverage there when changing
`dev.py`.

## Shell completion

`scripts/dev.py-completion.bash` provides bash tab-completion for the
subcommands, driven by `dev.py complete <prefix>`.

## `scripts/idf.sh`

Establishes the ESP-IDF environment, sourcing `$IDF_PATH/export.sh` when
needed. The root aggregator invokes `idf.py` through it (`set(IDF_RUNNER ...)`)
so an IDE-launched build works.
