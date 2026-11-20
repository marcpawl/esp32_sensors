#!/usr/bin/env python3
#
# ESP32 Solar-Powered Thermometer Logger — container task runner.
#
# Every build/flash/test/lint task runs inside the ESP-IDF dev container so the
# host stays clean and the toolchain is reproducible. This wrapper is the
# single entry point used by the CLion run configurations (see
# .idea/runConfigurations/*.xml) and the AGENTS.md workflow, so the compose
# specifics live in one place.
#
# Usage:
#   scripts/dev.py [pull|image|up|down|submodules|build|build-test-espidf-cpp|\
#                   flash|monitor|menuconfig|test-linux|test-esp32|self-check|\
#                   format|format-check|format-check-staged|pycodestyle|\
#                   clang-tidy|install-hooks|shell|ssh|clion-archive|\
#                   clion-compdb|clion-manual-test|clion-run-configs]
#   scripts/dev.py --help        # full help, including per-command details
#
# Environment:
#   ESP32_PORT   Serial device for flash/monitor (optional)
#                docker-compose.yml will pass into the container the value
#                on the host.
#   DEV_SSH_PORT Host port for `ssh` (optional; default 2222). Set it when
#                2222 is taken or several checkouts run at once.
#   GH_TOKEN     GitHub token for `gh` inside the container (optional;
#                GITHUB_TOKEN is accepted too). docker-compose.yml passes
#                whichever is set on the host, and `gh` prefers it over any
#                stored credential. See Developer.md, "GitHub CLI (`gh`) in
#                the container".
#
"""Container task runner for the ESP32 thermometer logger."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shlex
import socket
import subprocess
import sys
import tempfile
import time
from typing import Callable, NamedTuple

from clion_manual_test import (
    CLION_LAUNCHER_ENV,
    PROJECT_OPEN_PENDING,
    PROJECT_OPEN_READY,
    PROJECT_OPEN_WRONG,
    find_clion_launcher,
    prompt_yes_no,
    run_manual_test,
)

# Always operate from the repository root, regardless of the caller's cwd.
REPO_ROOT = Path(__file__).resolve().parent.parent

# Root under which the project is mounted inside the dev container. Paths in
# arguments addressed to the container (`build /project/components`) are mapped
# onto `REPO_ROOT`, and paths handed to a container re-exec are mapped back.
CONTAINER_PROJECT_ROOT = "/project"

# Directories whose C/C++ sources are checked/formatted by clang-format. The
# file list is discovered via git (`source_files`), and deliberately covers
# untracked files as well as tracked ones, so a source that exists but has not
# been `git add`ed is still formatted. Discovering the files through git keeps
# the walk to source trees (no build output, no missing-glob errors). Keep this
# Keep this in sync with the quality gate ordering documented in the skills.
ROOTS = (
    "mcgowan",
    "components",
    "scripts",
)
EXTENSIONS = (".cpp", ".hpp", ".h", ".cc", ".cxx")

# Vendored third-party trees (both the `third_party/json` component and the
# `third_party/sml` submodule). Nothing here is ours to fix, so it is excluded
# from every check: clang-format and pycodestyle skip it outright, and
# clang-tidy skips it through `_is_vendored`.
VENDORED_DIRS = ("third_party",)

# Python sources are linted by `pycodestyle` (see `cmd_pycodestyle`). Unlike
# the C/C++ list, the search covers the whole repository rather than ROOTS,
# for two reasons: the interesting files are extensionless Python shebang
# scripts (e.g. future tooling) and sources outside the C/C++ trees.
# `third_party/` is dropped by `_select_python_sources`, and `.gitignore`d
# build output never appears because git is the enumerator.
PYTHON_EXTENSIONS = (".py",)

# A first line naming a Python interpreter: `#!/usr/bin/env python3`,
# `#!/usr/bin/python`, `#!python3.12`, ... Matched against bytes, because the
# file is read in binary to avoid decoding a shebang that is not valid UTF-8.
PYTHON_SHEBANG = re.compile(rb"^#![^\n]*\bpython[0-9.]*\b")

# pycodestyle's own defaults are used deliberately: PEP 8, 79 columns. Passing
# `--max-line-length` or any other flag here would make this command and a bare
# `pycodestyle` disagree, so the repository conforms to the tool rather than
# the other way round. Kept as a constant so the argv is built in one place.
PYCODESTYLE_ARGS: tuple[str, ...] = ()

# Git submodules holding vendored third-party code the build needs. A bare
# clone leaves each submodule directory empty, so the build/test/lint commands
# fetch them first, and `scripts/dev.py submodules` (CI step) does so
# explicitly.
#
# `third_party/sml` is boost-ext/sml (https://github.com/boost-ext/sml),
# pinned to tag **v1.2.0** via the gitlink recorded in .gitmodules. The
# `components/sml` component exposes its headers to `liblogic`, which uses it
# for the application state machines. Its sources are NOT ours: they sit
# outside ROOTS (so clang-format never touches them) and are excluded from
# clang-tidy
# (see `_is_vendored`).
SUBMODULES = ("third_party/sml",)

# A file whose presence proves a submodule is actually checked out. Its absence
# means the gitlink was not fetched (fresh clone, or
# `--no-recurse-submodules`).
SUBMODULE_MARKERS = {
    "third_party/sml": "third_party/sml/include/boost/sml.hpp",
}

# clang-format style files. Style is discovered by searching *upward* from each
# source file, so a nested one (e.g. `DisableFormat: true` over vendored code)
# overrides the root `.clang-format`. `format-check-staged` must reproduce that
# search in its temp tree, or it would check a different style than `format`
# and reject commits that `format` leaves untouched.
STYLE_FILE_NAMES = (".clang-format", ".clang-format-ignore")

# Docker dev image: the prebuilt ESP-IDF v6.1 image plus this project's tooling
# (gh, PyGithub, esp-clang). It is pinned in docker-compose.yml too; the tag is
# never moved, so update it in both places (test_dev.py asserts they agree).
# `LOCAL_IMAGE` is only what `image` (a local rebuild) produces, for trying out
# Dockerfile changes.
DEV_IMAGE = "marcpawl/esp-idf:20261119c"
LOCAL_IMAGE = "esp32-sensors-dev:local"

# The image's Dockerfile and its build context. Both live in `docker/`, next to
# `docker-compose.yml` (whose `build.context` is `.` relative to its own
# directory), so `image` must not assume the repo root is the context.
DOCKER_DIR = "docker"
DOCKERFILE = f"{DOCKER_DIR}/Dockerfile"

# User baked into DEV_IMAGE, and where ccache lives under a writable HOME. A
# container run as any *other* uid (a host user, or a CI runner whose uid is
# 1001) has no passwd entry in the image, so `$HOME` would be unusable and
# ccache would fail with "ccache: error: Permission denied". `compose_run`
# therefore pins HOME (and CCACHE_DIR) explicitly.
CONTAINER_UID = 1000
HOME_CACHE_DIR = ".cache/ccache"

# SSH into the long-running dev container (`ssh`). The image's entrypoint
# starts sshd as the container user on this unprivileged port (see
# docker/Dockerfile), and docker-compose.yml publishes it on the host's
# loopback interface. sshd accepts the host's own public keys, which reach the
# container through the existing read-only `~/.ssh` bind mount, so no key is
# generated or copied.
DEV_SSH_SERVICE = "dev"
DEV_SSH_CONTAINER_PORT = 2222
DEV_SSH_HOST = "127.0.0.1"
DEV_SSH_USER = "dev"
DEV_SSH_PUBLIC_KEYS = (
    "authorized_keys",
    "id_ed25519.pub",
    "id_ecdsa.pub",
    "id_rsa.pub",
)
DEV_SSH_START_TIMEOUT_SECONDS = 30.0

# `clion-archive` seeds the remote backend archive that the Toolbox agent
# downloads into the container, so CLion can install the backend without
# fetching ~1.9 GB from the network. Toolbox names a downloaded archive
# `<first 9 hex of sha256>-<filename from the download URL>`, and its
# downloader treats an existing file at that path as a cached download.
TOOLBOX_DOWNLOAD_DIR = "/home/dev/.cache/JetBrains/Toolbox/download"
TOOLBOX_LOG = (
    "/home/dev/.local/share/JetBrains/Toolbox/logs/toolbox-cli.latest.log"
)
# A character class keeps `pkill -f` from matching the shell that runs it,
# whose command line also contains this text (the `ps | grep [p]attern` trick).
TOOLBOX_AGENT_REGEX = "com.jetbrains.toolbox.[M]ainKt"
CLION_ARCHIVE_PATTERN = "CLion-*.tar.gz"
# A running CLion has its install path in `/proc/<pid>/cmdline` (for example
# `.../Toolbox/apps/clion/bin/clion`). Matching a path component rather than
# the bare word "clion" avoids matching this script's own `clion-manual-test`
# argument. The optional version suffix and `-` cover `clion1` and
# `clion-radler`.
CLION_PROCESS_MARKER = re.compile(r"/clion(?:[0-9]+)?(?:/|$|[\s-])")

# ESP-IDF's locations in the dev image: `IDF_PATH` is a Docker ENV in the
# official base image, and `/opt/esp/entrypoint.sh` exports `IDF_TOOLS_PATH`
# before the container's command starts. sshd starts listening with a cleared
# environment (see docker/Dockerfile), so an SSH session inherits neither and
# `export.sh` would look for the toolchain under the user's home and fail. The
# remote command restores them as defaults, so the session uses the baked
# toolchain.
DEV_IDF_PATH = "/opt/esp/idf"
DEV_IDF_TOOLS_PATH = "/opt/esp"

FORMAT_ARGS = ("--dry-run", "--Werror")

# The repo root is not an ESP-IDF project; it aggregates the subprojects
# (each a standalone ESP-IDF project with its own build directory):
#
#   mcgowan/  ESP32-S3 firmware (hal_board)
#                                        -> mcgowan/build_esp32s3/mcgowan.bin
#   components/hal/tests/test-hal-stub/
#             host tests for components/hal/hal_stub (linux target)
#                                             -> .../test-hal-stub/build_linux
#   components/hal/tests/test-hal-common/
#             host tests for components/hal/hal_common (linux target)
#                                            -> .../test-hal-common/build_linux
#   components/utility/tests/test-utility/
#             host tests for components/utility/libutility (linux target)
#                                          -> .../test-utility/build_linux
#   components/logic/tests/test-logic/
#             host unit tests (linux target)      -> .../test-logic/build_linux
#   components/espidfcpp/tests/test-espidf-cpp/
#             host unit tests for components/espidfcpp/libespidfcpp
#                                     -> .../test-espidf-cpp/build_linux
#   components/simulator/tests/test-simulator/
#             host tests for components/simulator/libsimulator (linux target)
#                                          -> .../test-simulator/build_linux
#   components/simulator/exe/donny/
#             linux-emulated application (hal_stub, linux target)
#                                              -> .../donny/build_linux
#
# Build directories are named after the ESP-IDF target so the esp32s3 and
# linux builds stay separate.
# Directory (repo-relative) where each project the firmware and test commands
# operate on directly lives. Only the firmware sits at the repository root; the
# test projects live under `components/<layer>/tests/` and the simulator
# application under `components/simulator/exe/`. The set of subprojects is not
# this list: it is discovered on disk (see `_discover_projects`).
FIRMWARE_DIR = "mcgowan"
UTILITY_TEST_DIR = "components/utility/tests/test-utility"
LOGIC_TEST_DIR = "components/logic/tests/test-logic"
HAL_STUB_TEST_DIR = "components/hal/tests/test-hal-stub"
HAL_COMMON_TEST_DIR = "components/hal/tests/test-hal-common"
ESPIDF_CPP_TEST_DIR = "components/espidfcpp/tests/test-espidf-cpp"
TEST_SIMULATOR_DIR = "components/simulator/tests/test-simulator"
DONNY_DIR = "components/simulator/exe/donny"
ESP_TIMER_STUB_DIR = "components/hal/tests/esp_timer/stub"
ESP_TIMER_BOARD_DIR = "components/hal/tests/esp_timer/board"


class Project(NamedTuple):
    """An ESP-IDF project found on disk.

    `name` is the CMake `project()` name, which also names the application
    binary; `target` is the ESP-IDF target selected by the project's committed
    `sdkconfig.defaults`.
    """

    directory: Path
    name: str
    target: str


# A directory is an ESP-IDF project when its top-level CMakeLists.txt includes
# ESP-IDF's project.cmake. Components and the root aggregator do not, so this
# tells the projects apart from the component libraries they build.
ESPIDF_PROJECT_MARKER = "tools/cmake/project.cmake"

# A discovered `linux` project is a host test suite only when it sits under a
# directory with this name. That excludes host *applications* such as `donny`,
# which target `linux` but are not tests, and the on-target test projects,
# which target the chip.
TEST_PROJECT_DIRECTORY = "tests"

# Target assumed for a project whose committed defaults do not name one.
DEFAULT_TARGET = "esp32s3"


FW_BUILD_DIR = "build_esp32s3"
TEST_BUILD_DIR = "build_linux"
ROOT_BUILD_DIR = "build_root"

# CLion's compilation-database project is kept in its own directory: CLion
# writes a `.idea/` next to the compile_commands.json it imports, and doing
# that in the repo root would overwrite the tracked CMake project's config.
# The files it resolves are still rooted at /project; see AGENTS.md.
CLION_DB_DIRECTORY = ".clion"

# Shared CLion run configurations live here. The linux ELF run/debug configs
# can be regenerated with `scripts/dev.py clion-run-configs`.
CLION_RUN_CONFIG_DIR = ".idea/runConfigurations"

# Directories never descended into while discovering projects. Build output
# holds no sources, `.git` is not a tree of either, and the vendored trees are
# not ours to build. CLion's per-subproject build trees (`cmake-build-*`) are
# pruned by prefix.
DISCOVERY_PRUNE_DIRS = frozenset({
    ".git",
    *VENDORED_DIRS,
    FW_BUILD_DIR,
    TEST_BUILD_DIR,
    ROOT_BUILD_DIR,
})
DISCOVERY_PRUNE_PREFIXES = ("cmake-build-",)

# The CMake `project()` call (its name is the app binary's name) and the
# target line in `sdkconfig.defaults`.
_CMAKE_PROJECT_CALL = re.compile(
    r"^\s*project\s*\(\s*([^\s)#]+)", re.MULTILINE
)
_CMAKE_TARGET = re.compile(r'^\s*CONFIG_IDF_TARGET="([^"]+)"', re.MULTILINE)
# The target ESP-IDF records in the build tree's generated CMake cache.
_CMAKE_CACHE_TARGET = re.compile(r"^IDF_TARGET:[^=\n]*=(\S+)", re.MULTILINE)


def _read_text(path: Path) -> str | None:
    """The file's text, or None when it cannot be read. Pure."""
    try:
        return path.read_text(encoding="utf-8", errors="replace")
    except OSError:
        return None


def _project_target(directory: Path) -> str:
    """The ESP-IDF target a project builds for. Pure apart from reads.

    `sdkconfig.defaults` is what `idf.py` reads on a fresh clone, so it is
    authoritative; an existing `sdkconfig` is consulted only when the defaults
    name no target, so a stale generated file cannot override them. A project
    that names neither builds for the chip, like the firmware.
    """
    for name in ("sdkconfig.defaults", "sdkconfig"):
        text = _read_text(directory / name)
        if text is not None:
            match = _CMAKE_TARGET.search(text)
            if match:
                return match.group(1)
    return DEFAULT_TARGET


def _configured_target(build_dir: Path) -> str | None:
    """The target an existing build tree is configured for, or None. Pure.

    ESP-IDF writes the selected target into the build tree's `CMakeCache.txt`.
    A missing file or line means there is no usable configuration yet.
    """
    text = _read_text(build_dir / "CMakeCache.txt")
    if text is None:
        return None
    match = _CMAKE_CACHE_TARGET.search(text)
    return match.group(1) if match else None


def _needs_set_target(build_dir: Path, target: str) -> bool:
    """Whether `set-target` must run before building. Pure apart from reads.

    `set-target` deletes the build tree's ninja dependency log, so running it
    before every build recompiles every translation unit. A tree already
    configured for `target` needs only `build`.
    """
    return _configured_target(build_dir) != target


def _project_at(directory: Path) -> Project | None:
    """The ESP-IDF project rooted at `directory`, or None. Pure."""
    text = _read_text(directory / "CMakeLists.txt")
    if text is None or ESPIDF_PROJECT_MARKER not in text:
        return None
    match = _CMAKE_PROJECT_CALL.search(text)
    if match is None:
        return None
    return Project(directory, match.group(1), _project_target(directory))


def _discover_projects(directory: Path) -> list[Project]:
    """Every ESP-IDF project at or under `directory`, in path order. Pure.

    A project is a directory whose top-level CMakeLists.txt includes ESP-IDF's
    project.cmake (see `ESPIDF_PROJECT_MARKER`). Discovery stops at a project:
    its `main/` and `components/` directories belong to it rather than being
    projects of their own. Build output, `.git`, and the vendored third-party
    trees are never descended into.
    """
    directory = directory.resolve()
    projects: list[Project] = []
    for dirpath, dirnames, _ in os.walk(directory):
        path = Path(dirpath)
        dirnames[:] = sorted(
            name
            for name in dirnames
            if name not in DISCOVERY_PRUNE_DIRS
            and not name.startswith(DISCOVERY_PRUNE_PREFIXES)
        )
        project = _project_at(path)
        if project is not None:
            projects.append(project)
            dirnames[:] = []
    return projects


def _espidf_projects_under(
    directory: Path, root: Path = REPO_ROOT
) -> list[Project]:
    """Every ESP-IDF project at or under `directory`, in path order.

    A thin wrapper over `_discover_projects` with the `(directory, root)`
    signature the selection helpers take; the root plays no part in plain
    discovery. Pure.
    """
    return _discover_projects(directory)


def _is_host_test_project(project: Project, root: Path = REPO_ROOT) -> bool:
    """Whether `project` is a host test suite rather than an application."""
    if project.target != "linux":
        return False
    try:
        relative = project.directory.resolve().relative_to(root.resolve())
    except ValueError:
        return False
    return TEST_PROJECT_DIRECTORY in relative.parts


def _host_test_projects_under(
    directory: Path, root: Path = REPO_ROOT
) -> list[Project]:
    """Linux test projects at or under `directory`, in path order."""
    return [
        project
        for project in _discover_projects(directory)
        if _is_host_test_project(project, root)
    ]


def _repo_relative(path: Path, root: Path = REPO_ROOT) -> str:
    """`path` relative to the repository root, POSIX-style. Pure."""
    try:
        return path.resolve().relative_to(root.resolve()).as_posix()
    except ValueError:
        return path.resolve().as_posix()


def _project_compile_db(project: Project) -> Path:
    """The compile database `build` writes for `project`. Pure."""
    build_dir = TEST_BUILD_DIR if project.target == "linux" else FW_BUILD_DIR
    return project.directory / build_dir / "compile_commands.json"


def _clion_databases(root: Path = REPO_ROOT) -> list[tuple[Project, Path]]:
    """Databases to merge, in precedence order, paired with their projects.

    Firmware first, then the other chip projects, then the linux projects in
    path order. Because the merge takes the first entry for a source (see
    `_merge_compilation_databases`), a file compiled by several projects keeps
    the ESP-IDF include paths and defines the chip build sees. Host-only
    test files still come from their linux project. Missing databases are
    omitted, so an unbuilt project cannot block the others.
    """
    projects = sorted(
        _discover_projects(root),
        key=lambda project: (
            project.directory != root / FIRMWARE_DIR,
            project.target == "linux",
            project.directory.as_posix(),
        ),
    )
    return [
        (project, database)
        for project in projects
        if (database := _project_compile_db(project)).is_file()
    ]


def _merge_compilation_databases(databases: list[Path]) -> list[dict]:
    """One entry per source file, from the first database that lists it.

    Each entry is copied verbatim: the subprojects record container-absolute
    paths (`/project/...`, `/opt/esp/...`), which is what CLion needs when its
    backend runs inside the dev container.
    """
    entries: list[dict] = []
    seen: set[str] = set()
    for database in databases:
        for entry in json.loads(database.read_text()):
            source = entry["file"]
            if source in seen:
                continue
            seen.add(source)
            entries.append(entry)
    return entries


def _write_compilation_database(
    entries: list[dict], destination: Path
) -> None:
    """Write `entries` as JSON, replacing `destination` atomically.

    CLion may reload the file while it is being regenerated, so the JSON is
    written to a sibling temp file and renamed into place only once complete.
    """
    destination.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.NamedTemporaryFile(
        mode="w",
        encoding="utf-8",
        dir=destination.parent,
        prefix=f".{destination.name}.",
        suffix=".tmp",
        delete=False,
    ) as handle:
        temporary = Path(handle.name)
        try:
            json.dump(entries, handle, indent=2)
            handle.write("\n")
        except BaseException:
            temporary.unlink(missing_ok=True)
            raise
    os.replace(temporary, destination)


# The firmware app binary (named after the mcgowan project).
FW_BINARY = "mcgowan.bin"

# Host-side unit tests for the repository's scripts (stdlib `unittest`), run
# by `scripts/dev.py self-check`.
SELF_CHECK_MODULES = ("scripts.test_dev", "scripts.test_donny")
SELF_CHECK_MODULE = SELF_CHECK_MODULES[0]

# Source extensions clang-tidy analyzes. The compile database lists every
# translation unit, including the generated C/assembly ESP-IDF compiles; only
# C++ sources are ours to check.
TIDY_EXTENSIONS = (".cpp", ".cc", ".cxx")

# Build directories that hold the compile databases `build` writes. A
# directory-scoped `clang-tidy` (`clang-tidy components/`) reads databases
# under these names only, so CLion's `cmake-build-*` trees and stale copies of
# an old build are never consulted.
TIDY_BUILD_DIRS = (FW_BUILD_DIR, TEST_BUILD_DIR)

# GCC driver flags clang's driver rejects outright, or accepts only to warn
# "argument unused" (which `--warnings-as-errors` would turn into a failure).
# ESP-IDF passes them to the xtensa GCC for code-size reasons; they are
# meaningless to clang-tidy, which only parses (never code-generates).
TIDY_DROP_FLAGS = frozenset({
    "-c",
    "-fanalyzer",
    "-fno-jump-tables",
    "-fno-shrink-wrap",
    "-fno-test-coverage",
    "-fno-tree-switch-conversion",
    "-freorder-blocks",
    "-fstrict-volatile-bitfields",
    "-mlongcalls",
    "-mno-target-align",
})

# GCC flags whose *value* is the following argv element (`-o out.o`,
# `-MF dep.d`). Either form is dropped, together with its value.
TIDY_DROP_FLAGS_WITH_VALUE = frozenset({"-o", "-MF", "-MT", "-MQ"})

# GCC flags whose value is glued on with `=` (`-fmacro-prefix-map=a=b`).
TIDY_DROP_FLAG_PREFIXES = (
    "-ffile-prefix-map=",
    "-fdebug-prefix-map=",
    "-fdiagnostics-color",
    "-fmacro-prefix-map=",
    "-specs=",
    # The `-Wno-error=` and `-Wno-` companions to `-fanalyzer` name warnings
    # clang does not have, so clang would reject them as unknown options.
    "-Wno-error=analyzer-",
    "-Wno-analyzer-",
)


# The image (and, for the pinned image, docker-compose.yml) provides this
# marker file. A file rather than an environment variable, because sshd
# starts a session without the container's environment, which made a build
# over SSH look like a host build.
DEV_CONTAINER_MARKER = "/etc/esp32-sensors-dev-container"


def is_in_container():
    """True when this process runs inside the dev container."""
    return Path(DEV_CONTAINER_MARKER).exists()


def split_list(items, max_size):
    # Step through the list from 0 to n, skipping by max_size each time
    return [items[i:i + max_size] for i in range(0, len(items), max_size)]


def _source_file_command(roots: tuple[str, ...] = ROOTS) -> list[str]:
    """The `git ls-files` argv that enumerates the sources to format. Pure.

    `--cached` lists the files in the index (the tracked ones) and `--others`
    adds the files that exist in the working tree but are not tracked yet.
    Both are needed: enumerating only `--cached` meant a brand-new source that
    had not been `git add`ed was silently skipped by `format`, so the file
    stayed unformatted while `format-check-staged` (which runs on the staged
    content by definition) reported it. `format` must see every source under
    `ROOTS` that exists on disk, tracked or not.

    `--exclude-standard` applies `.gitignore`, so build trees and other
    generated output are still never formatted even though they are untracked.

    The pathspec is scoped to `roots` rather than the whole repository, so the
    caller controls the tree; `_select_sources` additionally drops
    `third_party/` (vendored, must stay byte-identical) even when a pathspec
    such as `.` covers it.
    """
    return [
        "git", "ls-files",
        "--cached", "--others", "--exclude-standard",
        "--", *roots,
    ]


def _select_sources(paths: list[str], root: Path = REPO_ROOT) -> list[str]:
    """Filter `paths` down to the C/C++ sources that exist on disk. Pure.

    Keeps only the `EXTENSIONS` suffixes (git lists every file type) and drops
    paths that are not regular files: a tracked file that has been deleted is
    still in the index, and clang-format cannot read it. Duplicates cannot
    arise — `git ls-files` prints each path once even when it is both cached
    and untracked — but the result is sorted for a stable, reviewable order.
    """
    return sorted({
        path
        for path in paths
        if path.endswith(EXTENSIONS)
        and not _is_vendored_path(path)
        and (root / path).is_file()
    })


def source_files(directories: list[Path] | None = None) -> list[str]:
    """Every C/C++ source under `directories`, tracked or untracked.

    This is the file list `format` and `format-check` operate on. With no
    `directories` the historical `ROOTS` trees are scanned; otherwise each
    directory is a `git ls-files` pathspec (repo-relative), so a subdirectory
    restricts the run and the repository root covers everything. Vendored trees
    are always filtered out. It includes files that have not been `git add`ed
    yet, so formatting a new source does not depend on staging it first;
    ignored files (build output) are still excluded by `--exclude-standard`.
    """
    roots = (
        tuple(_repo_relative(directory) for directory in directories)
        if directories else ROOTS
    )
    try:
        result = subprocess.run(
            _source_file_command(roots),
            cwd=REPO_ROOT,
            capture_output=True,
            text=True,
            check=True,
        )
    except subprocess.CalledProcessError as e:
        print(
            f"Git command failed with exit code {e.returncode}",
            file=sys.stderr,
        )
        print(f"Error output: {e.stderr}", file=sys.stderr)
        raise
    except FileNotFoundError:
        print(
            "Error: 'git' command-line tool not found on this system.",
            file=sys.stderr,
        )
        raise

    return _select_sources(result.stdout.splitlines())


def _cxx_sources_under(
    directory: Path, root: Path = REPO_ROOT
) -> list[str]:
    """Every C/C++ source at or under `directory`, in path order.

    The `format`/`format-check`/`clang-tidy` notion of "this directory can run
    the command": a directory with no source has nothing for those commands to
    do, so the selection climbs to a parent that has some (see
    `_escalated_directories`). `root` is unused; it keeps the
    `(directory, root)` signature the selection helpers take. Nearly pure
    (it shells out to git, like `source_files`).
    """
    return source_files([directory])


def _is_vendored_path(path: str) -> bool:
    """True when repo-relative `path` lives under a vendored tree. Pure.

    String-based rather than `Path`-based so it can be used while filtering
    before touching the filesystem; a top-level match only, so a directory
    *named* `third_party` deeper in the tree is not accidentally exempt.
    """
    return any(
        path == vendored or path.startswith(f"{vendored}/")
        for vendored in VENDORED_DIRS
    )


def _select_python_sources(
    paths: list[str], root: Path = REPO_ROOT
) -> list[str]:
    """Filter `paths` down to the Python files to lint. Pure.

    A path counts when it ends in `.py` **or** its first line is a Python
    shebang. The shebang half is what picks up `.githooks/pre-commit`, which is
    Python with no extension; it is the one place this check looks past the
    file name, and it costs a single small read per candidate.

    Vendored trees are dropped (`_is_vendored_path`) and paths that are not
    regular files are skipped: git still lists a tracked file that has been
    deleted, and pycodestyle cannot read it. The result is sorted for a stable,
    reviewable order. Pure except for reading first lines, and `root` is a
    parameter so it can be exercised against a scratch tree.
    """
    selected = set()
    for path in paths:
        if _is_vendored_path(path) or not (root / path).is_file():
            continue
        if path.endswith(PYTHON_EXTENSIONS):
            selected.add(path)
            continue
        with open(root / path, "rb") as handle:
            first_line = handle.readline()
        if PYTHON_SHEBANG.match(first_line):
            selected.add(path)
    return sorted(selected)


def python_sources() -> list[str]:
    """Every Python source in the repository, tracked or untracked.

    Repository-wide on purpose: see `PYTHON_EXTENSIONS`. It reuses
    `_source_file_command` with the repository root as the only pathspec, so
    the same `--cached --others --exclude-standard` semantics apply — a new
    file that has not been `git add`ed is still checked, and build output that
    `.gitignore` covers is not.
    """
    result = subprocess.run(
        _source_file_command(roots=(".",)),
        cwd=REPO_ROOT,
        capture_output=True,
        text=True,
        check=True,
    )
    return _select_python_sources(result.stdout.splitlines())


def _pycodestyle_command(files: list[str]) -> list[str]:
    """The `pycodestyle` argv for `files`. Pure (testable).

    `python3 -m pycodestyle` rather than the console script, so the checker is
    the one belonging to the interpreter that runs in the container (the
    ESP-IDF venv) even if PATH is odd — e.g. through a login shell.
    """
    return ["python3", "-m", "pycodestyle", *PYCODESTYLE_ARGS, *files]


def cmd_pycodestyle() -> int:
    """Python style check (pycodestyle, PEP 8) over the repository's Python.

    Covers `scripts/dev.py`, `scripts/test_dev.py`, and any Python shebang
    script; `third_party/` is excluded. pycodestyle's defaults are enforced
    (79 columns), so the result matches a bare `pycodestyle` run.
    """
    change_to_container()

    files = python_sources()
    if not files:
        print("pycodestyle: no Python sources to check", file=sys.stderr)
        return 1
    print(f"pycodestyle: checking {len(files)} Python sources")
    return run(_pycodestyle_command(files))


def run(cmd: list[str], cwd: Path | str = REPO_ROOT) -> int:
    """Run a command in `cwd` (default: repo root), inheriting stdio."""

    env = os.environ.copy()
    env["UID"] = str(os.getuid())
    env["GID"] = str(os.getgid())

    print(cmd)
    return subprocess.run(cmd, env=env, cwd=cwd).returncode


def _container_user() -> str:
    """The `--user` value for the dev container: this process's uid/gid.

    The container runs as the *calling* user, so every file it writes into the
    bind-mounted project is owned by that user on the host. That is the whole
    point: no chown, no root, no user-namespace remapping (a bind mount does
    not translate uids, so the uid the container runs as is the uid the host
    sees on the resulting files).

    On CI the runner's uid is 1001, not the image's 1000, which has no passwd
    entry in the image — see `_container_environment`.
    """
    return f"{os.getuid()}:{os.getgid()}"


def _container_home(container_user: str) -> str:
    """The `$HOME` the container has when running as `container_user`. Pure.

    The image's own user (uid 1000) owns `/home/dev`; any other uid has no
    passwd entry, so `$HOME` would resolve to `/` and everything that caches
    there would fail. `_container_environment` pins it to `/tmp` for those
    uids, and both locations are prepared (and bind-mounted) by the image and
    `docker-compose.yml`, so a path built from this answer always exists.
    """
    if int(container_user.partition(":")[0]) == CONTAINER_UID:
        return "/home/dev"
    return "/tmp"


def _container_environment(container_user: str) -> list[str]:
    """`compose run -e` flags giving `container_user` a usable `HOME`. Pure.

    A uid with no passwd entry in the image (any host user other than 1000,
    e.g. GitHub's runner at 1001) resolves `$HOME` to `/`, so tools that cache
    in `$HOME` fail: ccache reports "ccache: error: Permission denied" and the
    build dies. Point `HOME` (and thus ccache's cache) at world-writable `/tmp`
    for such users, which needs no root and no image change.

    The image's own user (uid 1000) owns `/home/dev`, so it is left alone and
    keeps the persisted cache volume mounted there.
    """
    if int(container_user.partition(":")[0]) == CONTAINER_UID:
        return []
    home = _container_home(container_user)
    return [
        "-e", f"HOME={home}",
        "-e", f"CCACHE_DIR={home}/{HOME_CACHE_DIR}",
    ]


def compose_run(args: list[str]) -> int:
    """Run a command inside the `dev` service defined by docker-compose.yml.

    The service is created on demand and removed afterwards (--rm), matching
    the documented `docker compose run --rm dev <cmd>` workflow. The container
    runs as the calling user (see `_container_user`) with a writable `HOME`
    (see `_container_environment`).
    """
    container_user = _container_user()
    command = ["docker", "compose", "-f", "docker/docker-compose.yml", "run",
               "--user", container_user,
               *_container_environment(container_user),
               "--rm", "dev"]
    command.extend(args)
    return run(command)


def change_to_container(trailing: list[str] | None = None):
    """If we are not in the container, then
       reexecute dev.py command in the container.

       Otherwise do noting.

       `trailing` replaces the arguments that follow the script path, so a
       command can pass normalized values (such as container paths) instead of
       the command line the user typed. With no arguments the current
       `sys.argv` is re-run verbatim.
    """
    if not is_in_container():
        arguments = sys.argv if trailing is None else [sys.argv[0], *trailing]
        rc = compose_run(arguments)
        sys.exit(rc)


def git(*args: str) -> subprocess.CompletedProcess:
    """Run git in the repo root and capture output."""
    return subprocess.run(
        ("git", *args),
        cwd=REPO_ROOT,
        check=True,
        capture_output=True,
        text=True,
    )


def _missing_submodules(markers: dict[str, str] = SUBMODULE_MARKERS,
                        root: Path = REPO_ROOT) -> list[str]:
    """Submodule paths that are not checked out yet. Pure (testable).

    A gitlink is recorded in the index whether or not the working tree actually
    has the sources, so presence is decided by the per-submodule marker file
    (e.g. `include/boost/sml.hpp`), which only exists once it is fetched.
    """
    return [
        path
        for path, marker in markers.items()
        if not (root / marker).is_file()
    ]


def _submodule_update_command(paths: list[str]) -> list[str]:
    """The `git submodule update` argv that fetches `paths`. Pure (testable).

    `--init` registers the submodule in `.git/config` and `--recursive` covers
    any nested submodules (boost-ext/sml has none today, but the flag keeps a
    future one from silently staying empty). Only the named paths are touched,
    so an unrelated submodule is left exactly as the caller left it.
    """
    return [
        "git", "submodule", "update", "--init", "--recursive", "--", *paths
    ]


def _ensure_submodules() -> int:
    """Check the vendored submodules out if they are missing. No container hop.

    Called at the start of `build`, `test`, and `clang-tidy`, so a bare clone
    builds without a manual `git submodule update`. When every marker is
    present this does nothing (the common case), so it costs one `stat` per
    submodule on every build.
    """
    missing = _missing_submodules()
    if not missing:
        return 0
    print(
        f"Fetching {len(missing)} missing git submodule(s): "
        f"{' '.join(missing)}"
    )
    return run(_submodule_update_command(missing))


def cmd_submodules() -> int:
    """Fetch (check out) the vendored git submodules, e.g. boost-ext/sml.

    Idempotent: submodules already checked out at the pinned commit are left
    alone. Run it once after a fresh clone, or whenever `.gitmodules` changes
    — `build`, `test`, and `clang-tidy` also call it automatically, and CI
    runs it as its own step so a fetch failure is reported on its own.
    """
    change_to_container()
    missing = _missing_submodules()
    if not missing:
        print("Submodules: already checked out")
        return 0
    return run(_submodule_update_command(missing))


def cmd_pull() -> int:
    """Pull the pinned prebuilt dev image from the registry."""
    if is_in_container():
        print("Image cannot be pulled while in container", file=sys.stderr)
        return 1
    return run(["docker", "pull", DEV_IMAGE])


def _image_command() -> list[str]:
    """`docker build` argv for the dev image. Pure, so it is unit-testable.

    The context is `docker/`, not the repo root: that is where the Dockerfile
    and docker-compose.yml live, and it is what compose's `build.context: .`
    resolves to. Using the repo root as the context would find no Dockerfile.
    """
    return ["docker", "build", "-t", LOCAL_IMAGE, "-f", DOCKERFILE,
            DOCKER_DIR]


def cmd_image() -> int:
    """Build the dev image locally from the Dockerfile. Not needed normally."""
    if is_in_container():
        print("Image cannot be built while in container", file=sys.stderr)
        return 1
    return run(_image_command())


def cmd_up() -> int:
    """Start the long-running dev container in the background.

    Runs `docker compose up -d` for the `dev` service, the same container
    `ssh` connects to and CLion's Dev Container uses. Host-only: compose is
    not available inside the container.
    """
    if is_in_container():
        print("up: run this on the host; inside use `shell`.", file=sys.stderr)
        return 1
    return run(_compose_command("up", "-d", DEV_SSH_SERVICE))


def cmd_down() -> int:
    """Stop and remove the long-running dev container.

    Runs `docker compose down --remove-orphans` with the repository's compose
    file, so a later `up` starts from a clean container and a fresh Toolbox
    log. Host-only, like `up`.
    """
    if is_in_container():
        print("down: run this on the host; inside use `shell`.",
              file=sys.stderr)
        return 1
    return run(_compose_command("down", "--remove-orphans"))


def _idf(directory: str, build_dir: str, *args: str) -> list[str]:
    """`idf.py` argv for a project directory relative to the repo root. Pure.

    The build directory lives inside the project directory.
    """
    return ["idf.py", "-C", directory, "-B", f"{directory}/{build_dir}",
            *args]


def _resolve_directory(
    argument: str, root: Path = REPO_ROOT, cwd: Path | None = None
) -> Path | None:
    """Resolve one `build`/`test`/`clang-tidy` argument to a directory.

    A relative path is resolved against `cwd` (the caller's working
    directory). An absolute path under the repository is taken as given, and
    `/project/...` is mapped onto `root`, so a command written for the
    container also works from the host. Returns None for anything that is not
    a directory inside the repository. Pure apart from `stat()`.
    """
    path = Path(argument)
    if path.is_absolute():
        try:
            path = root / path.relative_to(CONTAINER_PROJECT_ROOT)
        except ValueError:
            pass
    else:
        path = (cwd or Path.cwd()) / path

    path = path.resolve()
    if path.is_dir() and (path == root or root in path.parents):
        return path
    return None


def _start_directory(
    root: Path = REPO_ROOT, cwd: Path | None = None
) -> Path | None:
    """The directory `build`/`test` start from when given none. Pure.

    The caller's current directory, so `cd components/utility && dev.py
    test-linux` tests that subtree. None means the caller is outside the
    repository, which the commands reject rather than silently operating on
    a tree the caller did not name.
    """
    current = (cwd or Path.cwd()).resolve()
    if current == root or root in current.parents:
        return current
    return None


def _start_directories(
    arguments: list[str], root: Path = REPO_ROOT
) -> list[Path] | None:
    """The directories a command operates on, defaulting to the cwd.

    Returns None after printing when an argument is not a directory inside
    the repository, or when the caller's directory is outside it.
    """
    directories = _parse_directories(arguments, root=root)
    if directories is None:
        return None
    if directories:
        return directories
    start = _start_directory(root=root)
    if start is None:
        print(
            f"The current directory '{Path.cwd().resolve()}' is outside the "
            f"repository '{root}'; run from inside it or name a directory "
            f"under it.",
            file=sys.stderr,
        )
        return None
    return [start]


def _parse_directories(
    arguments: list[str], root: Path = REPO_ROOT, cwd: Path | None = None
) -> list[Path] | None:
    """Resolve `build`/`test`/`clang-tidy` arguments to repository directories.

    No arguments means an empty list; `build` and `test` then use
    `_start_directory`. A path that is not a directory inside the repository
    prints an error and returns None so the caller can exit non-zero.
    """
    if not arguments:
        return []
    directories: list[Path] = []
    for argument in arguments:
        directory = _resolve_directory(argument, root=root, cwd=cwd)
        if directory is None:
            print(
                f"'{argument}' is not a directory inside the repository "
                f"'{root}' (directories outside it are rejected).",
                file=sys.stderr,
            )
            return None
        directories.append(directory)
    return directories


def _carrying_directory(
    directory: Path,
    projects_at: Callable[[Path, Path], list[Project]],
    description: str,
    root: Path = REPO_ROOT,
) -> Path | None:
    """The nearest directory at or above `directory` the command can run in.

    A directory that has no `description` cannot run the command, so it is
    reported and its parent is tried instead; the climb repeats until a
    directory yields a project, and stops at the repository root. The root
    always has one of every project kind, so an empty root means the command
    would have nothing to do anywhere -- a bug in this script rather than a
    caller error. Pure apart from the report (testable).
    """
    root = root.resolve()
    current = directory.resolve()
    while True:
        if projects_at(current, root):
            return current
        if current == root:
            print(
                f"The repository root '{_repo_relative(root, root)}' has no "
                f"{description}; that is a bug in dev.py.",
                file=sys.stderr,
            )
            return None
        parent = current.parent
        print(
            f"No {description} in '{_repo_relative(current, root)}'; "
            f"using its parent '{_repo_relative(parent, root)}'.",
            file=sys.stderr,
        )
        current = parent


def _escalated_directories(
    directories: list[Path],
    projects_at: Callable[[Path, Path], list[Project]],
    description: str,
    root: Path = REPO_ROOT,
) -> list[Path] | None:
    """`directories`, each replaced by the closest ancestor it can run in.

    `projects_at(directory, root)` is the command's own selection: a
    non-empty result means the directory can run it. Each rejected directory
    has already been reported by `_carrying_directory`, and its parent is
    selected instead (see that helper for the climb). The result is
    de-duplicated so two arguments that climb to the same directory do not
    run the command there twice. None means the root could not run it, which
    `_carrying_directory` has reported as a bug. Pure apart from the reports
    (testable).
    """
    escalated: list[Path] = []
    for directory in directories:
        chosen = _carrying_directory(directory, projects_at, description, root)
        if chosen is None:
            return None
        if chosen not in escalated:
            escalated.append(chosen)
    return escalated


_ANALYZER_FLAG = "--analyzer"
_TRUE_VALUES = frozenset({"1", "true", "yes", "on"})
_FALSE_VALUES = frozenset({"0", "false", "no", "off"})


def _bool_value(text: str) -> bool:
    """Parse the boolean value accepted by `--analyzer=`."""
    lowered = text.lower()
    if lowered in _TRUE_VALUES:
        return True
    if lowered in _FALSE_VALUES:
        return False
    raise argparse.ArgumentTypeError(
        f"expected a boolean (true/false), got {text!r}"
    )


def _normalize_analyzer_argv(argv: list[str]) -> list[str]:
    """Turn a bare `--analyzer` into `--analyzer=true`.

    The value must be attached with `=`, so a bare `--analyzer` cannot consume
    the next token (a directory) as its value. Pure (testable).
    """
    return [
        f"{_ANALYZER_FLAG}=true" if token == _ANALYZER_FLAG else token
        for token in argv
    ]


def _analyzer_define(analyzer: bool) -> str:
    """The CMake cache value that turns the GCC analyzer on or off.

    Always passed explicitly (ON *and* OFF) so switching `--analyzer=false`
    cannot leave a previous ON in a subproject's cache.
    """
    return f"-DTHERMO_GCC_ANALYZER={'ON' if analyzer else 'OFF'}"


def _root_build_commands(analyzer: bool = False) -> list[list[str]]:
    """Configure and build the root aggregator project. Pure (testable).

    The root project is plain CMake (not ESP-IDF); its `SUBPROJECTS` cache
    variable defaults to every aggregate target, so `dev.py` passes no list
    and keeps no copy of one. `THERMO_GCC_ANALYZER` is passed on the configure
    command line and forwarded to each subproject's `idf.py`. Returns the
    configure command followed by the build command.
    """
    return [
        [
            "cmake",
            "-S", ".",
            "-B", ROOT_BUILD_DIR,
            _analyzer_define(analyzer),
        ],
        ["cmake", "--build", ROOT_BUILD_DIR],
    ]


def _build_all(analyzer: bool = False) -> int:
    """Build every subproject through the root aggregator. No container hop."""
    for cmd in _root_build_commands(analyzer):
        rc = run(cmd)
        if rc != 0:
            return rc
    return 0


def _build_project_directory(directory: Path, analyzer: bool = False) -> int:
    """Build one project directory, pinning the target its defaults select.

    A `linux` project is pinned to the `linux` target and builds into
    `build_linux`; everything else builds into `build_esp32s3`. The analyzer
    define is passed explicitly so a previous `--analyzer=false` cannot stick
    in the CMake cache.
    """
    define = _analyzer_define(analyzer)
    target = _project_target(directory)
    relative = _repo_relative(directory)
    build_dir = TEST_BUILD_DIR if target == "linux" else FW_BUILD_DIR
    if target == "linux" and _needs_set_target(directory / build_dir, target):
        rc = run(_idf(relative, build_dir, define, "--preview",
                      "set-target", "linux"))
        if rc != 0:
            return rc
    return run(_idf(relative, build_dir, define, "build"))


def _build_project(directory: str, analyzer: bool = False) -> int:
    """Build the project at a repo-relative `directory`. No container hop."""
    return _build_project_directory(REPO_ROOT / directory, analyzer)


def _build_project_directories(
    directories: list[Path], analyzer: bool = False
) -> int:
    """Build the project directories in order. No container hop."""
    for directory in directories:
        rc = _build_project_directory(directory, analyzer)
        if rc != 0:
            return rc
    return 0


def _build_directories(directories: list[Path]) -> list[Path] | None:
    """The project directories a `build` selection names. Pure.

    Every ESP-IDF project discovered at or under each argument is selected. An
    argument that contains none prints an error and returns None.
    """
    projects: list[Path] = []
    for directory in directories:
        found = [
            project.directory for project in _discover_projects(directory)
        ]
        if not found:
            print(
                f"No ESP-IDF projects in or under "
                f"'{_repo_relative(directory)}'.",
                file=sys.stderr,
            )
            return None
        for project in found:
            if project not in projects:
                projects.append(project)
    return projects


def cmd_build(arguments: list[str], analyzer: bool = False) -> int:
    """Build every ESP-IDF project in a directory, starting from the cwd.

    `build`                    → every project from the current directory
    `build .`                  → the same, spelled explicitly
    `build mcgowan`            → just the ESP32-S3 firmware, analyzer off
    `build components/utility` → every project in or under that directory
    `build --analyzer`         → any of the above, analyzer on
    `build --analyzer=false`   → explicitly off (the default)

    With no directory, the current directory is used; a caller outside the
    repository is an error. The projects under a directory are discovered on
    disk (see `_discover_projects`), so a new ESP-IDF project needs no entry
    here.

    A directory that has no ESP-IDF project at or under it cannot run the
    command, so the report names it, its parent is tried instead, and the
    climb repeats until one has a project (see `_escalated_directories`).
    `dev.py build .` in a component directory therefore builds the closest
    enclosing tree that does have a project, and the climb ends at the
    repository root; a root with no project at all is a bug in this script.
    """
    directories = _start_directories(arguments)
    if directories is None:
        return 1
    change_to_container(_project_container_arguments("build", directories,
                                                     analyzer))
    escalated = _escalated_directories(
        directories, _espidf_projects_under, "ESP-IDF projects"
    )
    if escalated is None:
        return 1
    directories = escalated
    rc = _ensure_submodules()
    if rc != 0:
        return rc
    if directories == [REPO_ROOT]:
        # The whole tree, however it was reached: one CMake build over the
        # root aggregator.
        return _build_all(analyzer)
    projects = _build_directories(directories)
    if projects is None:
        return 1
    return _build_project_directories(projects, analyzer)


def cmd_build_test_espidf_cpp() -> int:
    """Build the libespidfcpp unit tests for the ESP-IDF `linux` target."""
    change_to_container()
    rc = _ensure_submodules()
    if rc != 0:
        return rc
    return _build_project(ESPIDF_CPP_TEST_DIR)


def cmd_flash() -> int:
    """Flash then attach the serial monitor; exit it with Ctrl+]."""
    change_to_container()
    # No need to pass in the port since it is either in the
    # container environment of idf.py will hunt for the board
    # across all the tty devices.
    return run(_idf(FIRMWARE_DIR, FW_BUILD_DIR, "flash", "monitor"))


def cmd_monitor() -> int:
    """Attach the serial monitor; exit it with Ctrl+]."""
    change_to_container()
    return run(_idf(FIRMWARE_DIR, FW_BUILD_DIR, "monitor"))


def cmd_menuconfig() -> int:
    """Interactive Kconfig UI; edits sdkconfig (generated from defaults)."""
    change_to_container()
    return run(_idf(FIRMWARE_DIR, FW_BUILD_DIR, "menuconfig"))


def _run_suites(suites: list[tuple[Path, str]]) -> int:
    """Build and run the host test suites named by `(directory, elf)` pairs.

    No container hop; assumes it is already running inside the dev container.
    Each suite is pinned to `linux`, built, and its ELF run; the first failing
    suite stops the run with a non-zero status. `set-target` runs only for a
    tree that is not configured for `linux` yet, so a repeat run does not
    discard ninja's dependency log and recompile every source.
    """
    rc = _ensure_submodules()
    if rc != 0:
        return rc
    assert suites, "no host test suites selected"
    for directory, elf in suites:
        relative = _repo_relative(directory)
        commands = []
        if _needs_set_target(directory / TEST_BUILD_DIR, "linux"):
            commands.append(
                _idf(relative, TEST_BUILD_DIR, "--preview",
                     "set-target", "linux")
            )
        commands.extend((
            _idf(relative, TEST_BUILD_DIR, "build"),
            [f"./{relative}/{TEST_BUILD_DIR}/{elf}"],
        ))
        for cmd in commands:
            rc = run(cmd)
            if rc != 0:
                print(f"FAILED: host tests '{relative}'", file=sys.stderr)
                return 1

    print(
        "SUCCESS: host tests passed "
        f"({', '.join(_repo_relative(d) for d, _ in suites)})"
    )
    return 0


def _test_projects(
    directories: list[Path], root: Path = REPO_ROOT
) -> list[Project] | None:
    """The host test projects a `test` selection names, in path order. Pure.

    No directories means every suite in the repository. Each directory
    contributes every host test project at or under it. A directory that
    contributes no suite prints an error and returns None.
    """
    if not directories:
        return _host_test_projects_under(root, root)
    projects: list[Project] = []
    for directory in directories:
        found = _host_test_projects_under(directory, root)
        if not found:
            print(
                f"No host (`linux`) test projects in or under "
                f"'{_repo_relative(directory)}'.",
                file=sys.stderr,
            )
            return None
        for project in found:
            if project not in projects:
                projects.append(project)
    return projects


def cmd_test_linux(arguments: list[str] | None = None) -> int:
    """Run the host (`linux`) test suites from the current directory.

    A host test suite is every `linux`-target ESP-IDF project under a `tests/`
    directory (`_host_test_projects_under`), so a new suite is picked up
    without touching this command. With no directory, the current directory
    is used (a caller outside the repository is an error) in path order:
    `cd components/utility && dev.py test-linux` runs the utility suite, and
    `dev.py test-linux .` from the root runs every suite. The aggregate the
    pre-commit hook and CI call.

    A directory with no host suite at or under it cannot run the command, so
    its parent is tried instead, up to the repository root (see
    `_escalated_directories`); the climb is decided by whether suites exist
    there, not by the exit status of a suite that ran.
    """
    directories = _start_directories(arguments or [])
    if directories is None:
        return 1
    change_to_container(
        _project_container_arguments("test-linux", directories)
    )
    escalated = _escalated_directories(
        directories, _host_test_projects_under, "host (`linux`) test projects"
    )
    if escalated is None:
        return 1
    projects = _test_projects(escalated)
    if projects is None:
        return 1
    return _run_suites([
        (project.directory, f"{project.name}.elf") for project in projects
    ])


def _on_target_test_projects_under(
    directory: Path, root: Path = REPO_ROOT
) -> list[Project]:
    """Non-`linux` test projects at or under `directory`, in path order.

    An on-target test is an ESP-IDF project that targets a chip and lives under
    a `tests/` directory, so host applications such as `donny` and the
    firmware are excluded just as they are for `test-linux`.
    """
    projects: list[Project] = []
    for project in _discover_projects(directory):
        if project.target == "linux":
            continue
        try:
            relative = project.directory.resolve().relative_to(root.resolve())
        except ValueError:
            continue
        if TEST_PROJECT_DIRECTORY in relative.parts:
            projects.append(project)
    return projects


def _on_target_projects(
    directories: list[Path], root: Path = REPO_ROOT
) -> list[Project] | None:
    """The on-target test projects a `test-esp32` selection names, or None.

    No directories means the repository root. A directory that contributes no
    on-target test prints an error and returns None.
    """
    if not directories:
        return _on_target_test_projects_under(root, root)
    projects: list[Project] = []
    for directory in directories:
        found = _on_target_test_projects_under(directory, root)
        if not found:
            print(
                f"No on-target (`esp32`) test projects in or under "
                f"'{_repo_relative(directory)}'.",
                file=sys.stderr,
            )
            return None
        for project in found:
            if project not in projects:
                projects.append(project)
    return projects


def cmd_test_esp32(arguments: list[str] | None = None) -> int:
    """Build, flash, and run the on-target tests from the current directory.

    Every non-`linux` ESP-IDF project under a `tests/` directory is built and
    then run through its `test.py` driver, which drives the board (flash,
    serial log, sentinel). With no directory, the current directory is used;
    a caller outside the repository is an error. `ESP32_PORT` selects the
    board.

    A directory with no on-target test at or under it cannot run the command,
    so its parent is tried instead, up to the repository root (see
    `_escalated_directories`).
    """
    directories = _start_directories(arguments or [])
    if directories is None:
        return 1
    change_to_container(
        _project_container_arguments("test-esp32", directories)
    )
    escalated = _escalated_directories(
        directories, _on_target_test_projects_under,
        "on-target (`esp32`) test projects",
    )
    if escalated is None:
        return 1
    directories = escalated
    projects = _on_target_projects(directories)
    if projects is None:
        return 1
    rc = _ensure_submodules()
    if rc != 0:
        return rc
    for project in projects:
        relative = _repo_relative(project.directory)
        script = project.directory / "test.py"
        if not script.is_file():
            print(
                f"test-esp32: '{relative}' has no test.py driver.",
                file=sys.stderr,
            )
            return 1
        # Building first separates a compile failure from a test failure; the
        # driver's own `idf.py flash` then finds the tree up to date.
        rc = _build_project_directory(project.directory)
        if rc != 0:
            return rc
        rc = run([sys.executable, str(script)])
        if rc != 0:
            return rc
    return 0


def cmd_self_check() -> int:
    """Run the host-side unit tests for this script (stdlib `unittest`).

    Exercises `dev.py`'s own helpers (e.g. the command line `shell` builds)
    with the standard-library test runner, so no container or third-party
    dependency is needed. A few probes run the *pinned dev image* in a
    throwaway container to show that `dev.py` is on its PATH, so this command
    needs a Docker daemon and the image on the host; without either, those
    probes fail and name the pull command.

    Host-only. The suite pins the decisions the *host* side makes — above all
    whether a command re-execs itself under `docker compose` — and inside the
    container none of those decisions happen, so the run would pass vacuously
    instead of testing anything. The container also has no Docker daemon, so
    the image probes could not run. The command therefore refuses to run
    there, the same way `pull` and `image` do.
    """
    if is_in_container():
        print(
            "self-check: refusing to run inside the dev container.\n"
            "\n"
            "`self-check` is a HOST-side test of dev.py itself: it pins the\n"
            "choices the host makes, such as whether a command re-execs\n"
            "itself under `docker compose`. Inside the container those\n"
            "choices never happen, so the run would pass vacuously.\n"
            "\n"
            "Run it on the host instead:\n"
            "\n"
            "    ./scripts/dev.py self-check\n"
            "\n"
            "Inside the container use `./scripts/dev.py test-linux`, which\n"
            "builds and runs every discovered ESP-IDF `linux` test suite.",
            file=sys.stderr,
        )
        return 1
    return run(["python3", "-m", "unittest", "-v", *SELF_CHECK_MODULES])


def _format_directories(arguments: list[str]) -> list[Path] | None:
    """The directories a format command operates on, cwd by default. Pure."""
    return _start_directories(arguments)


def cmd_format(arguments: list[str]) -> int:
    """Apply the committed .clang-format (Google base, 4-space, 80 cols).

    With no directory, starts from the current directory; a caller outside the
    repository is an error. `third_party/` is never formatted.
    """
    directories = _format_directories(arguments)
    if directories is None:
        return 1
    change_to_container(_project_container_arguments("format", directories))
    escalated = _escalated_directories(
        directories, _cxx_sources_under, "C/C++ sources"
    )
    if escalated is None:
        return 1
    directories = escalated

    for file in source_files(directories):
        cmd = ["clang-format", "-i", file]
        rc = run(cmd)
        if rc != 0:
            print(f"Format failed: {file}", file=sys.stderr)
            return 1
    return 0


def cmd_format_check(arguments: list[str]) -> int:
    """CI-friendly check; non-zero exit if anything is unformatted.

    With no directory, starts from the current directory; a caller outside the
    repository is an error. `third_party/` is never checked.
    """
    directories = _format_directories(arguments)
    if directories is None:
        return 1
    change_to_container(_project_container_arguments("format-check",
                                                     directories))
    escalated = _escalated_directories(
        directories, _cxx_sources_under, "C/C++ sources"
    )
    if escalated is None:
        return 1
    directories = escalated

    for file in source_files(directories):
        cmd = ["clang-format", "--dry-run", "--Werror", file]
        rc = run(cmd)
        if rc != 0:
            print(f"Format check failed: {file}", file=sys.stderr)
            return 1
    return 0


def _tidy_compile_arguments(argv: list[str]) -> list[str]:
    """Strip the GCC-driver flags clang cannot use from one compile command.

    ESP-IDF compiles with Xtensa GCC, so the compilation database records
    flags the Espressif `clang-tidy` driver (LLVM) rejects: `-mlongcalls`,
    `-mno-target-align`, `-fstrict-volatile-bitfields`,
    `-fno-tree-switch-conversion`, `-fno-jump-tables`, `-freorder-blocks`,
    `-fanalyzer` (GCC's static analyzer; clang has no such option) and
    `-specs=...` are GCC-only, and `-fmacro-prefix-map=` is not a clang flag
    spelling clang accepts. The `-Wno-error=analyzer-...` and
    `-Wno-analyzer-...` forms configured alongside it are dropped for the same
    reason. Left in, they surface as `clang-diagnostic-error`s (or "argument
    unused" warnings, which `--warnings-as-errors` promotes).

    `argv` is the whole command line, so `argv[0]` (the compiler) is dropped:
    clang-tidy takes the driver from its own argv. The trailing source file is
    **kept**, because clang derives the compilation job from it — without one
    the driver reports "no input files" and every unit fails. Pure (no I/O), so
    it is unit-testable.
    """
    kept = []
    skip_value = False
    for token in argv[1:]:
        if skip_value:
            skip_value = False
            continue
        if token in TIDY_DROP_FLAGS_WITH_VALUE:
            skip_value = True
            continue
        if token in TIDY_DROP_FLAGS:
            continue
        if token.startswith(TIDY_DROP_FLAG_PREFIXES):
            continue
        kept.append(token)
    return kept


def _compiler_system_include_flags(
    compiler: str, args: list[str]
) -> list[str]:
    """Ask `compiler` for its system include dirs, as clang `-isystem` flags.

    clang's driver resolves `<cstdint>`, `<string>`, ... from its own resource
    directory, which has no libstdc++/newlib headers for the Xtensa target (and
    no `x86_64` multilib either). GCC does know where they are, so we ask it
    (`-E -v`) and pass the answer to clang as `-isystem`, together with
    `-nostdinc` so clang does *not* also search its own, incompatible copy.
    """
    command = [compiler, "-E", "-x", "c++", "-v",
               *[a for a in args if not a.startswith("-I")], "/dev/null"]
    result = subprocess.run(command, capture_output=True, text=True)
    match = re.search(
        r"#include <\.\.\.> search starts here:\n(.*?)\nEnd of search list",
        result.stderr,
        re.DOTALL,
    )
    if match is None:
        return []
    return [f"-isystem{line.strip()}" for line in match.group(1).splitlines()]


def _tidy_target_arguments(compiler: str, args: list[str]) -> list[str]:
    """clang flags making the analysis target match what `compiler` compiles.

    The compile database says *how* to compile but not *for what*: the target
    triple comes from the driver's name, which clang-tidy replaces with its own
    (defaulting to a host target). Taking `-dumpmachine` from the recorded
    compiler restores it — `xtensa-esp-elf` for the firmware,
    `x86_64-linux-gnu` for the `linux`-target host projects.

    `-Wno-unused-command-line-argument` keeps a stray GCC-only flag from
    failing the run: clang ignores those, and a diagnostic about the flag says
    nothing about our code.

    `-nostdinc` stops clang from *also* searching its own resource directory,
    which would shadow the compiler's headers with an incompatible copy.
    """
    machine = subprocess.run(
        [compiler, "-dumpmachine"], capture_output=True, text=True
    )
    triple = machine.stdout.strip()
    flags = ["-Wno-unused-command-line-argument"]
    if triple:
        flags.insert(0, f"--target={triple}")
    system_flags = _compiler_system_include_flags(compiler, args)
    if system_flags:
        # Only suppress clang's own search when there is something to replace
        # it with; otherwise every `<cstdint>` include would fail.
        flags.append("-nostdinc")
        flags.extend(system_flags)
    return flags


def _is_vendored(relative: Path) -> bool:
    """True when `relative` (repo-relative) lives inside a vendored submodule.

    Vendored sources are not ours to fix, so neither clang-format nor
    clang-tidy may report on them. `ROOTS` already keeps them out of the
    formatted file list (they live under `third_party/`, which is not a root);
    this is the matching guard for clang-tidy, whose input is the compile
    database and so is not limited to `ROOTS`. Pure (testable).
    """
    return any(
        relative == Path(sub) or Path(sub) in relative.parents
        for sub in SUBMODULES
    )


def _unique_tidy_units(
    compilations: list[dict], root: Path = REPO_ROOT
) -> list[dict]:
    """The C++ compilation-database entries to analyze, one per source file.

    A shared source (`components/logic/liblogic/src/payload.cpp`, most of the
    HAL) is
    compiled by three or four subprojects, so the union of their databases
    repeats it. Analyzing it once is enough, and taking the *first* occurrence
    keeps the verdict stable: the projects are listed firmware-first, so a
    shared file is checked as the chip build sees it.

    Entries outside `root` (ESP-IDF's own components, the generated
    `project_elf_src_*.c`), vendored submodule sources (see `_is_vendored`),
    and non-C++ sources are skipped. Pure (no I/O), so it is unit-testable.
    """
    seen = set()
    units = []
    for entry in compilations:
        source = Path(entry["file"])
        if source.suffix not in TIDY_EXTENSIONS:
            continue
        try:
            relative = source.relative_to(root)
        except ValueError:
            continue
        if _is_vendored(relative):
            continue
        if relative in seen:
            continue
        seen.add(relative)
        units.append(entry)
    return units


def _tidy_database(units: list[dict], destination: Path) -> None:
    """Write a compilation database clang can consume for `units`.

    Each entry is rewritten from the recorded GCC command line into a
    clang-compatible one (see `_tidy_compile_arguments`) plus the target flags
    from `_tidy_target_arguments`. `-fsyntax-only` keeps clang from looking for
    an output file, since clang-tidy only needs the parse.
    """
    # The target flags depend only on the compiler and its non-`-I` arguments
    # (see `_compiler_system_include_flags`), so they are probed once per
    # distinct command line rather than once per translation unit.
    cache: dict[tuple[str, tuple[str, ...]], list[str]] = {}
    entries = []
    for unit in units:
        argv = unit.get("arguments")
        if argv is None:
            argv = shlex.split(unit["command"])
        compiler = argv[0]
        args = _tidy_compile_arguments(argv)
        key = (compiler, tuple(a for a in args if not a.startswith("-I")))
        if key not in cache:
            cache[key] = _tidy_target_arguments(compiler, args)
        entries.append({
            "directory": unit["directory"],
            "file": unit["file"],
            "arguments": ["clang++", *cache[key], "-fsyntax-only", *args],
        })
    destination.write_text(json.dumps(entries, indent=2))


def _tidy_units(directory: Path) -> list[dict] | None:
    """The C++ units of one project directory, or None if its build is missing.

    Seeds the analysis from the project's compile database, then reduces it to
    one entry per source file (see `_unique_tidy_units`).
    """
    target = _project_target(directory)
    build_dir = TEST_BUILD_DIR if target == "linux" else FW_BUILD_DIR
    database = directory / build_dir / "compile_commands.json"
    if not database.is_file():
        relative = _repo_relative(directory)
        print(
            f"clang-tidy: '{relative}' has no "
            f"{_repo_relative(database)}; "
            f"run `scripts/dev.py build {relative}` first.",
            file=sys.stderr,
        )
        return None
    return _unique_tidy_units(json.loads(database.read_text()))


def _parse_tidy_scopes(arguments: list[str]) -> list[Path] | None:
    """Resolve `clang-tidy`'s directory arguments, cwd by default.

    Invalid arguments, and a caller outside the repository, print an error and
    return None so the caller can exit non-zero. From the repository root the
    scope is every discovered project, so a missing compile database is named
    per project; from a subdirectory it is that directory.
    """
    if arguments:
        return _parse_directories(arguments)
    directories = _start_directories([])
    if directories is None:
        return None
    if directories == [REPO_ROOT]:
        return [project.directory for project in _discover_projects(REPO_ROOT)]
    return directories


def _discover_tidy_databases(root: Path = REPO_ROOT) -> list[Path]:
    """The compile databases a directory scope may draw units from.

    Every `build_linux`/`build_esp32s3` tree under `root` (see
    `TIDY_BUILD_DIRS`), with the analyzed subprojects first and, among the
    rest, `build_linux` before `build_esp32s3`: a source file that several
    projects compile then keeps the flags of the first database that lists it.
    For the analyzed subprojects that is the firmware, as `_unique_tidy_units`
    documents; for the projects found only by a directory scope it is the host
    build, which is the variant CI runs (and the chip build of a shared test
    file can produce spurious analyzer results). Vendored submodule trees are
    skipped, and build trees are not descended into.
    """
    projects = sorted(
        _discover_projects(root),
        key=lambda project: (
            project.directory != root / FIRMWARE_DIR,
            project.target != "linux",
            project.directory.as_posix(),
        ),
    )
    known = []
    for project in projects:
        build_dir = (
            TEST_BUILD_DIR if project.target == "linux" else FW_BUILD_DIR
        )
        database = project.directory / build_dir / "compile_commands.json"
        if database.is_file():
            known.append(database)

    found = set()
    for dirpath, dirnames, filenames in os.walk(root):
        if Path(dirpath).name in TIDY_BUILD_DIRS:
            if "compile_commands.json" in filenames:
                found.add(Path(dirpath) / "compile_commands.json")
            dirnames[:] = []
            continue
        dirnames[:] = [
            name
            for name in sorted(dirnames)
            if name not in (".git", *VENDORED_DIRS)
        ]
    discovered = sorted(
        found - set(known),
        key=lambda path: (path.parent.name != TEST_BUILD_DIR, str(path)),
    )
    return [*known, *discovered]


def _tidy_units_under(
    directory: Path, databases: list[Path], root: Path = REPO_ROOT
) -> list[dict]:
    """The C++ units of `databases` whose source is under `directory`.

    Entries whose source file no longer exists are skipped: a stale build tree
    (an old `build_linux` left beside sources that have since moved) would
    otherwise hand clang-tidy a path it cannot read.
    """
    compilations: list[dict] = []
    for database in databases:
        for entry in json.loads(database.read_text()):
            source = Path(entry["file"])
            if source != directory and directory not in source.parents:
                continue
            if not source.is_file():
                continue
            compilations.append(entry)
    return _unique_tidy_units(compilations, root=root)


def _container_path(path: Path, root: Path = REPO_ROOT) -> str:
    """`path` as the container sees it (`/project/...`). Pure."""
    try:
        relative = path.relative_to(root)
    except ValueError:
        return path.as_posix()
    return (Path(CONTAINER_PROJECT_ROOT) / relative).as_posix()


def _project_container_arguments(
    command: str, directories: list[Path], analyzer: bool = False
) -> list[str]:
    """The `dev.py COMMAND` arguments to re-run inside the container. Pure.

    Every directory becomes its container path, so a host path such as
    `/home/me/esp32_sensors/components` re-executes as `/project/components`
    whatever the caller's host working directory was.
    """
    arguments = [command]
    if analyzer:
        arguments.append(f"{_ANALYZER_FLAG}=true")
    arguments.extend(_container_path(directory) for directory in directories)
    return arguments


def _tidy_container_arguments(
    directories: list[Path], fix: bool = False
) -> list[str]:
    """The `dev.py clang-tidy` arguments to re-run inside the container.

    Directories become container paths (see `_project_container_arguments`).
    Pure.
    """
    arguments = _project_container_arguments("clang-tidy", directories)
    if fix:
        arguments.insert(1, "--fix")
    return arguments


def _tidy_command(
    units: list[dict], build_path: Path, fix: bool = False
) -> list[str]:
    """The clang-tidy argv `cmd_clang_tidy` hands to `run()`. Pure.

    `build_path` is the directory holding the `compile_commands.json` that
    seeds the analysis (clang-tidy's `-p`), not the file itself.

    `--fix` applies clang-tidy's suggested fixes in place, so the files it
    rewrites are the ones in the compile database (inside the container, the
    bind-mounted working tree). Fixes are not necessarily clang-formatted:
    run `scripts/dev.py format` afterwards.
    """
    command = [
        "clang-tidy",
        "-p", str(build_path),
        "--header-filter", _tidy_header_filter(),
        "--exclude-header-filter", _tidy_exclude_header_filter(),
        "--warnings-as-errors=*",
        "--quiet",
    ]
    if fix:
        command.append("--fix")
    command.extend(unit["file"] for unit in units)
    return command


def cmd_clang_tidy(directories: list[Path], fix: bool = False) -> int:
    """Static analysis (clang-tidy) over the C/C++ sources in directories.

    Every argument is a directory inside the repository
    (`components/hal/`). A directory that is itself an ESP-IDF project is
    analyzed from that project's compile database; any other covers every C++
    source below it, including sources that belong to no analyzed project
    (e.g. `components/hal/tests/esp_timer/board/`), and draws them from every
    compile database under `TIDY_BUILD_DIRS`.

    Imports the ESP-IDF compile databases so clang-tidy sees the exact flags,
    defines, and include paths each file is really built with, including the
    `hal_board`/`hal_stub` split that differs per project.

    Nothing is auto-built: a project with no compile database is reported and
    the run fails, so CI cannot quietly analyze stale or partial input. Use
    `scripts/dev.py build` first.

    Uses the repository's `.clang-tidy`, which opts in to the check set this
    repository enforces (merged with clang-tidy's default set), and reports
    only diagnostics in this repository's own headers, not ESP-IDF's or
    libstdc++'s. Any finding is an error (`--warnings-as-errors=*`).

    With `fix`, `--fix` is passed to clang-tidy so its suggested fixes are
    applied in place. Findings that cannot be fixed still fail the run, and
    the rewritten code should be re-formatted (`scripts/dev.py format`).
    """
    change_to_container(_tidy_container_arguments(directories, fix))

    escalated = _escalated_directories(
        directories, _cxx_sources_under, "C/C++ sources"
    )
    if escalated is None:
        return 1
    directories = escalated

    rc = _ensure_submodules()
    if rc != 0:
        return rc

    units: list[dict] = []
    databases: list[Path] | None = None
    for directory in directories:
        if _project_at(directory) is not None:
            batch = _tidy_units(directory)
            if batch is None:
                return 1
            units.extend(batch)
            continue
        if databases is None:
            databases = _discover_tidy_databases()
        units.extend(_tidy_units_under(directory, databases))
    units = _unique_tidy_units(units)

    if not units:
        print(
            "clang-tidy: no C++ sources to analyze; "
            "run `scripts/dev.py build` first.",
            file=sys.stderr,
        )
        return 1

    with tempfile.TemporaryDirectory() as tmp:
        tmp_dir = Path(tmp)
        _tidy_database(units, tmp_dir / "compile_commands.json")
        suffix = " (applying fixes)" if fix else ""
        print(f"clang-tidy: analyzing {len(units)} C++ sources{suffix}")
        return run(_tidy_command(units, tmp_dir, fix))


def _tidy_header_filter() -> str:
    """Only report diagnostics in *our* headers, not ESP-IDF's or libstdc++'s.

    clang-tidy names a diagnostic by the file it lands in, and a template error
    often lands in a system header. Restricting to the repository's own
    directories keeps the signal on code we can fix.
    """
    roots = "|".join(re.escape(root) for root in ROOTS if root != "scripts")
    return rf"^{re.escape(str(REPO_ROOT))}/({roots})/"


def _tidy_exclude_header_filter() -> str:
    """Keep generated build output out of the report.

    Every build directory holds ESP-IDF's generated `config/sdkconfig.h`, and
    its macros are not ours to edit: the next build rewrites the file, so a
    fix there is lost. The header filter would otherwise report them.
    """
    build_dirs = "|".join(re.escape(name) for name in TIDY_BUILD_DIRS)
    # LLVM's regex engine (what clang-tidy uses) has no non-capturing groups,
    # so this sticks to plain ones.
    return rf"^{re.escape(str(REPO_ROOT))}/(.*/)?({build_dirs})/"


def cmd_install_hooks() -> int:
    """Activate the version-controlled git hooks (.githooks/pre-commit).

    The hook rejects commits whose staged C/C++ files are not
    clang-formatted.
    """
    rc = run([
        "git", "-C", str(REPO_ROOT), "config", "core.hooksPath", ".githooks"
    ])
    if rc == 0:
        print("Installed git hooks: core.hooksPath -> .githooks")
    return rc


# Root under which the project is mounted inside the dev container. The
# `shell -C DIR` option resolves relative directories against this.
CONTAINER_PROJECT_ROOT = "/project"


def _resolve_container_directory(directory: str) -> str:
    """Resolve a `shell -C DIR` value to an absolute in-container path.

    The value is interpreted as a path *in the container*. A relative path is
    taken relative to the project mount root (e.g. `test` -> `/project/test`);
    an absolute path is used verbatim.
    """
    path = Path(directory)
    if path.is_absolute():
        return path.as_posix()
    return (Path(CONTAINER_PROJECT_ROOT) / path).as_posix()


def _build_shell_command(
    extra: list[str], directory: str | None = None
) -> list[str]:
    """Build the `bash` argv that `cmd_shell` runs.

    Pure (no I/O), so it is unit-testable without a container hop. Returns the
    full argv to hand to `run()`.

    With no `extra`, an interactive login shell is started (optionally after a
    `cd`). Otherwise the caller's arguments form a shell command line that is
    run through a login shell so PATH and the ESP-IDF environment (sourced by
    the login profile) are available and shell syntax like pipes and globs
    works: `shell ls -1d / | wc -l`.

    The arguments are joined *verbatim* (no extra quoting): `shell "echo hi |
    wc -l"` must reach bash as `echo hi | wc -l`, not as a single quoted token.
    Everything after the `-c` string is `$0`/positional, so leading-dash
    arguments are never mistaken for bash options — no `--` is needed.
    """
    # Optionally prefix a `cd` so the command (or the interactive shell) starts
    # in the requested directory.
    prefix = []
    if directory is not None:
        target = _resolve_container_directory(directory)
        prefix = ["cd", shlex.quote(target), "&&"]

    if not extra:
        if not prefix:
            return ["bash", "-l"]
        # `cd DIR && exec bash -l` keeps the login shell (and thus the sourced
        # ESP-IDF environment) while honoring the requested working directory.
        return ["bash", "-l", "-c", " ".join([*prefix, "exec", "bash", "-l"])]

    inner = " ".join([*prefix, *extra])
    return ["bash", "-l", "-c", inner]


def cmd_shell(extra: list[str], directory: str | None = None) -> int:
    """Interactive shell with the ESP-IDF environment sourced.

    With no extra arguments this starts an interactive login shell. Any
    arguments after `shell` are executed in that shell instead, e.g.:

        scripts/dev.py shell ls -1d /

    Use `-C DIR` / `--directory DIR` to change into DIR before running the
    command. DIR is a path in the container: a relative path is resolved under
    the project mount (`test` -> /project/test), an absolute path is used
    as-is:

        scripts/dev.py shell -C test ls -1
        scripts/dev.py shell -C /project/test idf.py build

    """
    change_to_container()
    return run(_build_shell_command(extra, directory))


def _compose_command(*args: str) -> list[str]:
    """The `docker compose` argv for this repository. Pure (testable)."""
    return ["docker", "compose", "-f", "docker/docker-compose.yml", *args]


def _host_ssh_public_keys(home: Path | None = None) -> list[Path]:
    """The host files whose public keys the container's sshd accepts. Pure.

    `docker/Dockerfile` lists these same names in `AuthorizedKeysFile`, and
    `docker-compose.yml` mounts the host's `~/.ssh` read-only at the
    container's home, so an existing key needs no copying or generation.
    `authorized_keys` covers keys supplied by an agent or a non-standard file
    name; the `id_*.pub` files are the default keys `ssh` itself offers.
    """
    ssh_dir = (home or Path.home()) / ".ssh"
    return [
        ssh_dir / name
        for name in DEV_SSH_PUBLIC_KEYS
        if (ssh_dir / name).is_file()
    ]


def _ssh_remote_command(extra: list[str]) -> str:
    """The single command string `ssh` runs inside the container. Pure.

    sshd starts a session with an almost empty environment (`PATH`, `HOME`,
    `USER`, ...) rather than the container's, so the ESP-IDF locations are
    restored here before the shell runs: without `IDF_TOOLS_PATH`,
    `export.sh` looks for a virtualenv under `$HOME/.espressif` and fails.

    An interactive session then relies on the login shell's `~/.bashrc` to
    source `export.sh`, exactly as `shell` does. A command has no interactive
    shell to do that, so the script is sourced here as well, with its banner
    on stderr so the command's stdout stays clean for pipelines.

    It is the same login shell `shell` uses, starting in `/project`. SSH joins
    its command arguments with spaces, so the argv is quoted into one string
    here rather than passed as separate elements.
    """
    setup = [
        f'export IDF_TOOLS_PATH="${{IDF_TOOLS_PATH:-{DEV_IDF_TOOLS_PATH}}}"',
        f'export IDF_PATH="${{IDF_PATH:-{DEV_IDF_PATH}}}"',
    ]
    if extra:
        setup.append('{ . "$IDF_PATH/export.sh"; } >&2')

    command = _build_shell_command(extra, directory=CONTAINER_PROJECT_ROOT)
    command[-1] = " && ".join([*setup, command[-1]])
    return shlex.join(command)


def _ssh_command(port: int, extra: list[str]) -> list[str]:
    """The `ssh` argv `cmd_ssh` runs. Pure (testable).

    Host-key checking is disabled deliberately: the listener is on the host's
    loopback interface and the image bakes a fixed host key, so there is no
    remote party to verify; authentication is the developer's key. No `-i` is
    passed, so `ssh` uses the host's normal keys, config, and agent — the very
    keys the container's sshd was given through the `~/.ssh` mount.

    An interactive session (`extra` empty) allocates a TTY; a command runs
    without one, so `dev.py ssh ls -l` can be used in a pipeline.
    """
    command = [
        "ssh",
        "-p", str(port),
        "-o", "StrictHostKeyChecking=no",
        "-o", "UserKnownHostsFile=/dev/null",
        "-o", "LogLevel=ERROR",
    ]
    if not extra:
        command.append("-t")
    command.append(f"{DEV_SSH_USER}@{DEV_SSH_HOST}")
    command.append(_ssh_remote_command(extra))
    return command


def _clion_archive_candidates(home: Path | None = None) -> list[Path]:
    """CLion backend archives in the host's Downloads directory. Pure."""
    downloads = (home or Path.home()) / "Downloads"
    return sorted(downloads.glob(CLION_ARCHIVE_PATTERN))


def _sha256_file(path: Path) -> str:
    """The hex SHA-256 of a file, read in bounded chunks. Pure I/O."""
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def _clion_archive_name(archive: Path, digest: str) -> str:
    """The Toolbox cache name for an archive. Pure.

    Toolbox names a downloaded archive `<first 9 sha256 hex>-<filename from
    the download URL>`; the prefix is what lets it find a cached download by
    content instead of fetching the URL again.
    """
    return f"{digest[:9]}-{archive.name}"


def _clion_archive_destination(name: str) -> str:
    """Where Toolbox looks for a downloaded archive in the container. Pure."""
    return f"{TOOLBOX_DOWNLOAD_DIR}/{name}"


def _scp_command(port: int, source: Path, destination: str) -> list[str]:
    """The `scp` argv that copies a host archive into the container. Pure.

    The flags mirror `_ssh_command`: the listener is on the host's loopback
    interface with a fixed image host key, and authentication is the host's
    own key.
    """
    return [
        "scp",
        "-P", str(port),
        "-o", "StrictHostKeyChecking=no",
        "-o", "UserKnownHostsFile=/dev/null",
        "-o", "LogLevel=ERROR",
        str(source),
        f"{DEV_SSH_USER}@{DEV_SSH_HOST}:{destination}",
    ]


def _compose_ssh_port(output: str) -> int | None:
    """The host port in `docker compose port` output, or None. Pure.

    The output is one `host:port` line (e.g. `127.0.0.1:2222`). Returning the
    published port rather than assuming `DEV_SSH_PORT` means a moved or
    explicitly chosen port still works.
    """
    for line in output.splitlines():
        _, _, port = line.strip().rpartition(":")
        if port.isdigit():
            return int(port)
    return None


def _published_ssh_port() -> int | None:
    """Ask compose which host port is published for the container's sshd."""
    result = subprocess.run(
        _compose_command(
            "port", DEV_SSH_SERVICE, str(DEV_SSH_CONTAINER_PORT)
        ),
        cwd=REPO_ROOT,
        capture_output=True,
        text=True,
    )
    if result.returncode != 0:
        return None
    return _compose_ssh_port(result.stdout)


def _wait_for_ssh(port: int, seconds: float | None = None) -> bool:
    """Wait until `port` accepts a TCP connection on the host. Pure I/O."""
    deadline = time.monotonic() + (
        DEV_SSH_START_TIMEOUT_SECONDS if seconds is None else seconds
    )
    while time.monotonic() < deadline:
        try:
            with socket.create_connection((DEV_SSH_HOST, port), timeout=1):
                return True
        except OSError:
            time.sleep(0.2)
    return False


def _ensure_ssh_service() -> tuple[int, int | None]:
    """Start (or reuse) the dev container and return `(up_rc, ssh_port)`.

    Shared by `ssh` and `clion-archive`. A non-zero `up_rc` is the `docker
    compose up` return code and is returned to the caller unchanged;
    `ssh_port` is None when compose published no port or the container never
    started serving SSH, after printing the reason.
    """
    rc = run(_compose_command("up", "-d", DEV_SSH_SERVICE))
    if rc != 0:
        return rc, None

    port = _published_ssh_port()
    if port is None:
        print(
            "ssh: `docker compose port dev 2222` reported no published port; "
            "check docker/docker-compose.yml.",
            file=sys.stderr,
        )
        return 0, None

    if not _wait_for_ssh(port):
        # A container created before the SSH entrypoint existed does not run
        # sshd yet; ask it to start the server explicitly.
        run(_compose_command(
            "exec", "-d", DEV_SSH_SERVICE, "/usr/local/bin/dev-ssh-server"
        ))
        if not _wait_for_ssh(port):
            print(
                "ssh: the dev container is not serving SSH on port "
                f"{port}. The pinned image may predate the sshd setup; "
                "rebuild and publish a new image tag (see docker/AGENTS.md).",
                file=sys.stderr,
            )
            return 0, None
    return 0, port


def _stop_toolbox_agent(port: int) -> None:
    """Stop the Toolbox agent so it cannot overwrite a seeded archive.

    `pkill` exits 1 when nothing matched, which is not an error here. The
    agent gets a few seconds to finish a write, then SIGKILL if still alive.
    """
    pattern = shlex.quote(TOOLBOX_AGENT_REGEX)
    script = (
        f"pkill -TERM -f {pattern} || true; "
        "i=0; "
        f"while pgrep -f {pattern} >/dev/null; do "
        "i=$((i + 1)); "
        '[ "$i" -ge 20 ] && break; '
        "sleep 0.25; "
        "done; "
        f"pkill -KILL -f {pattern} || true"
    )
    run(_ssh_command(port, ["sh", "-c", script]))


def _remote_sha256(port: int, path: str) -> str | None:
    """The SHA-256 of `path` inside the container, or None on failure."""
    command = _ssh_command(port, ["sha256sum", path])
    print(command)
    result = subprocess.run(
        command, cwd=REPO_ROOT, capture_output=True, text=True
    )
    if result.returncode != 0:
        return None
    fields = result.stdout.split()
    return fields[0] if fields else None


def cmd_ssh(extra: list[str]) -> int:
    """SSH into the running dev container from the host.

    With no arguments this opens an interactive login shell; with arguments it
    runs them as a command, as the container user, from `/project`:

        scripts/dev.py ssh
        scripts/dev.py ssh ls -l

    The container is started (or reused) with `docker compose up -d dev`, and
    the command runs through a login shell, so the ESP-IDF environment is
    sourced exactly as it is for `shell`. The host's `~/.ssh` keys authenticate
    the connection; there is no separate key to manage.

    Host-only: inside the container there is nothing to connect to, so use
    `shell` there.
    """
    if is_in_container():
        print(
            "ssh: run this on the host. Inside the container use `shell`.",
            file=sys.stderr,
        )
        return 1

    if not _host_ssh_public_keys():
        print(
            "ssh: no public key found in ~/.ssh "
            f"({', '.join(DEV_SSH_PUBLIC_KEYS)}).\n"
            "Create one with: ssh-keygen -t ed25519",
            file=sys.stderr,
        )
        return 1

    rc, port = _ensure_ssh_service()
    if rc != 0:
        return rc
    if port is None:
        return 1
    return run(_ssh_command(port, extra))


def cmd_clion_archive(archive: str | None) -> int:
    """Seed the container's Toolbox cache with a local CLion backend archive.

    CLion's Remote Connections flow installs the backend through the Toolbox
    agent, which downloads a ~1.9 GB `CLion-*.tar.gz` into the container. This
    command copies an archive from the host into the exact cache path the
    agent looks for, so it installs from the local file instead of the
    network:

        scripts/dev.py clion-archive
        scripts/dev.py clion-archive ~/Downloads/CLion-2026.3.0.1.tar.gz

    With no argument it uses the only `~/Downloads/CLion-*.tar.gz`; with
    several it stops and asks for one. The Toolbox cache name is
    `<first 9 hex of sha256>-<filename>`.

    A running Toolbox agent is stopped first (an in-flight download would
    otherwise overwrite the seeded file), together with the interrupted
    download's `.tmp`/`journal` state. The copy is made as a `.part` file and
    renamed only after its remote SHA-256 matches the local one.

    Host-only, like `ssh`: it needs the host's archive, keys and scp.
    """
    if is_in_container():
        print(
            "clion-archive: run this on the host. Inside the container use "
            "`shell`.",
            file=sys.stderr,
        )
        return 1

    if not _host_ssh_public_keys():
        print(
            "clion-archive: no public key found in ~/.ssh "
            f"({', '.join(DEV_SSH_PUBLIC_KEYS)}).\n"
            "Create one with: ssh-keygen -t ed25519",
            file=sys.stderr,
        )
        return 1

    if archive is None:
        candidates = _clion_archive_candidates()
        if not candidates:
            print(
                "clion-archive: no ~/Downloads/"
                f"{CLION_ARCHIVE_PATTERN}; pass the archive path explicitly.",
                file=sys.stderr,
            )
            return 1
        if len(candidates) > 1:
            print(
                "clion-archive: several CLion archives found; pass one "
                "explicitly:",
                file=sys.stderr,
            )
            for candidate in candidates:
                print(f"  {candidate}", file=sys.stderr)
            return 1
        source = candidates[0]
    else:
        source = Path(archive).expanduser()

    if not source.is_file():
        print(f"clion-archive: {source} is not a file", file=sys.stderr)
        return 1

    print(f"clion-archive: hashing {source}")
    digest = _sha256_file(source)
    name = _clion_archive_name(source, digest)
    destination = _clion_archive_destination(name)
    part = f"{destination}.part"

    rc, port = _ensure_ssh_service()
    if rc != 0:
        return rc
    if port is None:
        return 1

    print("clion-archive: stopping the Toolbox agent")
    _stop_toolbox_agent(port)
    # An interrupted download leaves a DIRTY DiskLruCache entry; without
    # this it resumes and can replace the archive seeded below.
    run(_ssh_command(port, [
        "rm", "-f",
        f"{TOOLBOX_DOWNLOAD_DIR}/*.tmp",
        f"{TOOLBOX_DOWNLOAD_DIR}/journal",
    ]))
    run(_ssh_command(port, ["mkdir", "-p", TOOLBOX_DOWNLOAD_DIR]))

    if run(_scp_command(port, source, part)) != 0:
        return 1

    if _remote_sha256(port, part) != digest:
        run(_ssh_command(port, ["rm", "-f", part]))
        print(
            "clion-archive: the copied archive failed its SHA-256 check",
            file=sys.stderr,
        )
        return 1

    if run(_ssh_command(port, ["mv", "-f", part, destination])) != 0:
        return 1

    print(f"clion-archive: seeded {destination}")
    print(
        "Reconnect CLion; it will reuse this archive instead of downloading."
    )
    return 0


def cmd_clion_compdb() -> int:
    """Merge the subprojects' compile databases for CLion's code model.

    The root CMake project is an aggregator with no C/C++ code model, so
    hovering an ESP-IDF declaration cannot reach its Doxygen comments. This
    command combines the databases that `scripts/dev.py build` writes into
    `.clion/compile_commands.json`, which CLion opens as a compilation
    database project. The first database listing a source wins: firmware,
    then the other chip projects, then the linux projects, so shared sources
    keep the ESP-IDF include paths that make tooltips work. Open that file as
    a project inside the dev container, set its project root to /project,
    and choose a container-local System toolchain.
    """
    change_to_container()

    found = _clion_databases()
    if not found:
        print(
            "clion-compdb: no compile databases found; "
            "run `scripts/dev.py build` first.",
            file=sys.stderr,
        )
        return 1

    databases = [database for _, database in found]
    entries = _merge_compilation_databases(databases)

    known = set(databases)
    for project in _discover_projects(REPO_ROOT):
        database = _project_compile_db(project)
        if database not in known:
            print(
                f"clion-compdb: no {_repo_relative(database)}; "
                f"build {_repo_relative(project.directory)} first",
                file=sys.stderr,
            )

    destination = REPO_ROOT / CLION_DB_DIRECTORY / "compile_commands.json"
    _write_compilation_database(entries, destination)
    print(
        f"clion-compdb: wrote {len(entries)} entries to "
        f"{_repo_relative(destination)}"
    )
    return 0


def _cmdline_names_clion(cmdline: str) -> bool:
    """True when a `/proc/<pid>/cmdline` string belongs to CLion. Pure.

    The arguments are NUL-separated; the search works with either separator.
    """
    text = cmdline.replace("\0", " ")
    return CLION_PROCESS_MARKER.search(text) is not None


def _local_clion_is_running(
    proc_root: Path = Path("/proc"),
    own_pid: int | None = None,
) -> bool:
    """True when a CLion IDE process is running on the host.

    Scans `/proc/<pid>/cmdline` for CLion's install path. The current process
    is skipped because it may itself be the `clion-manual-test` command.
    Unreadable entries (short-lived processes, other users) are ignored: a
    running CLion stays alive long enough to be seen.
    """
    skip_pid = os.getpid() if own_pid is None else own_pid
    try:
        entries = list(proc_root.iterdir())
    except OSError:
        return False
    for entry in entries:
        if not entry.name.isdigit() or int(entry.name) == skip_pid:
            continue
        try:
            raw = (entry / "cmdline").read_bytes()
        except OSError:
            continue
        cmdline = raw.decode("utf-8", "replace")
        if _cmdline_names_clion(cmdline):
            return True
    return False


def _launch_clion(command: str) -> int:
    """Start CLion detached on the host, without waiting for it to exit."""
    argv = shlex.split(command)
    if not argv:
        print("clion-manual-test: empty CLion launcher", file=sys.stderr)
        return 1
    try:
        subprocess.Popen(argv, cwd=REPO_ROOT, start_new_session=True)
    except OSError as error:
        print(f"clion-manual-test: could not start CLion: {error}",
              file=sys.stderr)
        return 1
    return 0


def _dev_py_command(command: str) -> int:
    """Run another `dev.py` command through its documented entry point."""
    script = REPO_ROOT / "scripts" / "dev.py"
    return run([sys.executable, str(script), command])


def _is_clion_archive_log_line(line: str) -> bool:
    """True for a Toolbox log line about the CLion backend archive. Pure."""
    lowered = line.lower()
    return "clion-" in lowered and ".tar.gz" in lowered


def _is_cached_clion_archive_line(line: str) -> bool:
    """True when a log line proves the archive came from the local cache.

    Toolbox names the cached file by checksum and logs a "will be reused"
    line that also mentions the remote URL. It then verifies the local file
    with a "Correct checksum for <cache path>" line. Neither is a download.
    """
    lowered = line.lower()
    if not _is_clion_archive_log_line(lowered):
        return False
    return (
        "will be reused as a cached previous download" in lowered
        or f"correct checksum for {TOOLBOX_DOWNLOAD_DIR.lower()}/" in lowered
    )


def _backend_download_log_verdict(stdout: str) -> tuple[bool, str]:
    """Classify a fresh Toolbox log. Pure."""
    lines = [line.strip() for line in stdout.splitlines()]
    cached = [line for line in lines if _is_cached_clion_archive_line(line)]
    downloads = [
        line
        for line in lines
        if _is_clion_archive_log_line(line)
        and not _is_cached_clion_archive_line(line)
        and re.search(r"download|fetch", line, re.IGNORECASE)
    ]
    if downloads:
        return False, (
            "Toolbox log recorded a CLion archive download: "
            + downloads[-1][:120]
        )
    if cached:
        return True, (
            "Toolbox log shows the CLion archive was reused from cache"
        )
    return False, (
        "Toolbox log has no evidence that the CLion archive was reused"
    )


def _backend_download_log_check() -> tuple[bool, str]:
    """Check the fresh Toolbox log for local reuse of the seeded archive.

    The wizard removes the container first, so this log belongs only to the
    run being tested. A seeded archive must produce a cache-reuse or local
    checksum line, not a remote download.
    """
    command = _compose_command(
        "exec", "-T", DEV_SSH_SERVICE, "cat", TOOLBOX_LOG,
    )
    result = subprocess.run(
        command, cwd=REPO_ROOT, capture_output=True, text=True,
    )
    if result.returncode != 0:
        detail = result.stderr.strip() or f"exit {result.returncode}"
        return False, f"could not read {TOOLBOX_LOG}: {detail}"

    return _backend_download_log_verdict(result.stdout)


_REMOTE_SERVER_MODE_RE = re.compile(r"serverMode,\s*(/[^,\]\s]+)")


def _opened_project_verdict(stdout: str) -> tuple[str, str]:
    """Classify the opened remote project from a Toolbox log. Pure.

    CLion remote connections started via Toolbox record `serverMode,
    <directory>` in the log. No match means the connection has not happened
    yet; a match other than `/project` is a wrong project, not a timeout.
    """
    matches = _REMOTE_SERVER_MODE_RE.findall(stdout)
    if not matches:
        return PROJECT_OPEN_PENDING, (
            "Toolbox log has no remote project directory yet."
        )

    directory = matches[-1]
    if directory == "/project":
        return PROJECT_OPEN_READY, (
            "Toolbox log shows the remote project directory `/project`."
        )

    return PROJECT_OPEN_WRONG, (
        "Toolbox log shows remote project directory "
        f"`{directory}`; expected `/project`."
    )


def _remote_project_dir_verdict(stdout: str) -> tuple[bool, str]:
    """Classify the most recent remote project directory from Toolbox log.

    The repository must be opened as `/project` so shared `.idea` run/debug
    configurations are available. Both a missing and a wrong directory fail.
    """
    status, detail = _opened_project_verdict(stdout)
    if status == PROJECT_OPEN_READY:
        return True, detail
    if status == PROJECT_OPEN_PENDING:
        return False, (
            "Toolbox log has no remote project directory; connect CLion to "
            "the remote project `/project`."
        )
    return False, detail


def _opened_project_log_check() -> tuple[str, str]:
    """Poll the Toolbox log while CLion is opening the remote project."""
    command = _compose_command(
        "exec", "-T", DEV_SSH_SERVICE, "cat", TOOLBOX_LOG,
    )
    result = subprocess.run(
        command, cwd=REPO_ROOT, capture_output=True, text=True,
    )
    if result.returncode != 0:
        detail = result.stderr.strip() or f"exit {result.returncode}"
        return PROJECT_OPEN_PENDING, f"could not read {TOOLBOX_LOG}: {detail}"

    return _opened_project_verdict(result.stdout)


def _remote_project_dir_log_check() -> tuple[bool, str]:
    """Check the Toolbox log for the remote project directory in use."""
    command = _compose_command(
        "exec", "-T", DEV_SSH_SERVICE, "cat", TOOLBOX_LOG,
    )
    result = subprocess.run(
        command, cwd=REPO_ROOT, capture_output=True, text=True,
    )
    if result.returncode != 0:
        detail = result.stderr.strip() or f"exit {result.returncode}"
        return False, f"could not read {TOOLBOX_LOG}: {detail}"

    return _remote_project_dir_verdict(result.stdout)


def cmd_clion_manual_test() -> int:
    """Start CLion and guide the host-only manual tooltip check (never CI).

    Preflight checks the host, the absence of `CI`, and a CLion launcher. The
    launcher is taken from `CLION_LAUNCHER`, then PATH, then the JetBrains
    Toolbox locations. The wizard first refuses when CLion is already running
    locally, then kills and deletes the dev container (`down`), then runs `up`,
    `build`, `clion-compdb`, and `clion-run-configs`, optionally seeds a local
    backend archive, starts CLion detached, and asks for an explicit yes/no
    after each manual IDE step. Before the first prompt it polls the Toolbox
    log for the opened remote project, allowing a 10-minute wait that the user
    may extend; a wrong project fails immediately.
    """
    if is_in_container():
        print("clion-manual-test: run this on the host.", file=sys.stderr)
        return 1
    if os.environ.get("CI"):
        print("clion-manual-test: refusing to run on CI.", file=sys.stderr)
        return 1

    launcher = find_clion_launcher()
    if not launcher:
        print(
            "clion-manual-test: no CLion launcher found; "
            f"set {CLION_LAUNCHER_ENV}",
            file=sys.stderr,
        )
        return 1

    archives = _clion_archive_candidates()
    if len(archives) > 1:
        print(
            "clion-manual-test: several CLion archives found; pass one "
            "explicitly to `clion-archive`",
            file=sys.stderr,
        )
        for archive in archives:
            print(f"  {archive}", file=sys.stderr)
        return 1
    archive_path = str(archives[0]) if archives else None

    return run_manual_test(
        launcher=launcher,
        check_clion_running=_local_clion_is_running,
        stop_container=lambda: _dev_py_command("down"),
        start_container=lambda: _dev_py_command("up"),
        build=lambda: _dev_py_command("build"),
        clion_compdb=lambda: _dev_py_command("clion-compdb"),
        clion_run_configs=lambda: _dev_py_command("clion-run-configs"),
        launch_clion=_launch_clion,
        prompt=prompt_yes_no,
        archive_path=archive_path,
        seed_archive=lambda: _dev_py_command("clion-archive"),
        check_backend_log=_backend_download_log_check,
        check_remote_project_dir=_remote_project_dir_log_check,
        check_opened_project=_opened_project_log_check,
    )


# ---------------------------------------------------------------------------
# CLion Linux ELF run/debug configuration generation
# ---------------------------------------------------------------------------

_IDF_SUBPROJECT_CALL = re.compile(
    r"_idf_subproject\(\s*([^\s)]+)\s+([^\s)]+)\s+"
    r"(build_linux|build_esp32s3)\b"
)

# Profiles that are always kept in .idea/cmake.xml (firmware/chip targets and
# any other non-generated profile). Linux ELF profiles for discovered linux
# executable projects are regenerated by `clion-run-configs`.
_CLION_FIRMWARE_PROFILE = (
    '      <configuration PROFILE_NAME="Espressif ESP32-S3" ENABLED="true" '
    'CONFIG_NAME="Debug" TOOLCHAIN_NAME="Default" '
    'GENERATION_DIR="$PROJECT_DIR$/mcgowan/cmake-build-esp32s3" '
    'GENERATION_OPTIONS="-DIDF_TARGET=esp32s3 -DCMAKE_BUILD_TYPE=Debug" />'
)


def _linux_subproject_ids(root: Path = REPO_ROOT) -> dict[str, str]:
    """Map a linux subproject's repo-relative directory to its aggregate id.

    The root aggregator's `_idf_subproject(<id> <dir> build_linux ...)` calls
    define the `<id>_build` target name CLion run configurations must use.
    """
    text = _read_text(root / "CMakeLists.txt") or ""
    ids: dict[str, str] = {}
    for match in _IDF_SUBPROJECT_CALL.finditer(text):
        aggregate_id, rel_dir, build_dir = match.groups()
        if build_dir == "build_linux":
            ids[rel_dir] = aggregate_id
    return ids


def _clion_linux_generation_suffix(aggregate_id: str) -> str:
    """The `cmake-build-linux-*` directory suffix for a linux profile."""
    if aggregate_id == "test-esp_timer-stub":
        return "esp-timer-stub"
    return aggregate_id


def _clion_linux_profile_name(aggregate_id: str) -> str:
    """The CLion CMake profile name for a linux executable project."""
    if aggregate_id == "donny":
        return "Linux app (donny)"
    if aggregate_id == "test-esp_timer-stub":
        return "Host tests (esp_timer stub)"
    return f"Host tests ({aggregate_id})"


def _clion_linux_run_slug(aggregate_id: str) -> str:
    """Filename slug for a `Linux_ELF__<slug>__container.xml` config."""
    return aggregate_id.replace("-", "_")


def _clion_linux_descriptors(
    root: Path = REPO_ROOT,
) -> list[dict[str, str]]:
    """Linux executable projects with their CLion/run/debug metadata.

    Each descriptor carries the information needed to write the matching
    `.idea/cmake.xml` profile and `.idea/runConfigurations/Linux_ELF__*` file.
    Projects are returned in path order for deterministic output.
    """
    ids = _linux_subproject_ids(root)
    descriptors: list[dict[str, str]] = []
    for project in _discover_projects(root):
        if project.target != "linux":
            continue
        rel_dir = _repo_relative(project.directory, root)
        aggregate_id = ids.get(rel_dir, project.name)
        profile_name = _clion_linux_profile_name(aggregate_id)
        descriptors.append({
            "aggregate_id": aggregate_id,
            "project_name": project.name,
            "rel_dir": rel_dir,
            "elf_path": f"{rel_dir}/{TEST_BUILD_DIR}/{project.name}.elf",
            "target_name": f"{aggregate_id}_build",
            "profile_name": profile_name,
            "config_name": profile_name,
            "display_name": f"{aggregate_id} (linux ELF, container)",
            "slug": _clion_linux_run_slug(aggregate_id),
            "generation_dir": (
                f"$PROJECT_DIR$/{rel_dir}/cmake-build-linux-"
                f"{_clion_linux_generation_suffix(aggregate_id)}"
            ),
            "kind": "app" if aggregate_id == "donny" else "test",
        })
    return descriptors


def _render_linux_run_configuration(descriptor: dict[str, str]) -> str:
    """The XML content of a Linux ELF run/debug configuration."""
    aggregate_id = descriptor["aggregate_id"]
    rel_dir = descriptor["rel_dir"]
    display_name = descriptor["display_name"]
    target_name = descriptor["target_name"]
    config_name = descriptor["config_name"]
    elf_path = descriptor["elf_path"]
    if descriptor["kind"] == "app":
        subject = f"the {aggregate_id} Linux ELF"
        build_comment = (
            f"  The before-launch step builds it with the root aggregator's "
            f"`{target_name}`\n"
            f"  target (`idf.py build` in {rel_dir}). RUN_PATH names\n"
            f"  the ELF explicitly: the root CMake project is "
            f"`project(... NONE)` and\n"
            f"  exposes only UTILITY targets, so no executable can be "
            f"selected as\n"
            f"  TARGET_NAME. Selecting this configuration also activates the "
            f"matching\n"
            f"  .idea/cmake.xml profile.\n"
        )
        debug_comment = (
            "  The linux app carries the same debug flags as the host "
            "tests, and the\n"
            "  CLion backend runs in the same container as the ELF, so Debug "
            "works with\n"
            "  breakpoints.\n"
        )
    elif aggregate_id == "test-esp_timer-stub":
        subject = "the esp_timer host-test Linux ELF"
        build_comment = (
            f"  The before-launch step builds it with the root aggregator's\n"
            f"  `{target_name}` target (`idf.py build` in\n"
            f"  {rel_dir}). RUN_PATH names the ELF explicitly:\n"
            f"  the root CMake project is `project(... NONE)` and exposes "
            f"only UTILITY\n"
            f"  targets, so no executable can be selected as TARGET_NAME. "
            f"Selecting this\n"
            f"  configuration also activates the matching .idea/cmake.xml "
            f"profile.\n"
        )
        debug_comment = (
            "  Host tests compile with -gdwarf-4 -ggdb -Og and the CLion "
            "backend runs in\n"
            "  the same container as the ELF, so Debug works with "
            "breakpoints.\n"
        )
    else:
        subject = f"the {aggregate_id} Linux ELF"
        build_comment = (
            f"  The before-launch step builds it with the root aggregator's\n"
            f"  `{target_name}` target (`idf.py build` in\n"
            f"  {rel_dir}). RUN_PATH names the ELF explicitly:\n"
            f"  the root CMake project is `project(... NONE)` and exposes "
            f"only UTILITY\n"
            f"  targets, so no executable can be selected as TARGET_NAME. "
            f"Selecting this\n"
            f"  configuration also activates the matching .idea/cmake.xml "
            f"profile.\n"
        )
        debug_comment = (
            "  Host tests compile with -gdwarf-4 -ggdb -Og and the CLion "
            "backend runs in\n"
            "  the same container as the ELF, so Debug works with "
            "breakpoints.\n"
        )
    return (
        '<?xml version="1.0" encoding="UTF-8"?>\n'
        "<!--\n"
        f"  Run/debug {subject} inside the dev container.\n"
        "\n"
        f"{build_comment}"
        "\n"
        f"{debug_comment}"
        "-->\n"
        '<component name="ProjectRunConfigurationManager">\n'
        f'  <configuration default="false" name="{display_name}" '
        'type="CMakeRunConfiguration" factoryName="Application" '
        'REDIRECT_INPUT="false" ELEVATE="false" '
        'USE_EXTERNAL_CONSOLE="false" EMULATE_TERMINAL="false" '
        'PASS_PARENT_ENVS_2="true" '
        'PROJECT_NAME="esp32_sensors_subprojects" '
        f'TARGET_NAME="{target_name}" CONFIG_NAME="{config_name}" '
        f'RUN_PATH="$PROJECT_DIR$/{elf_path}">\n'
        '    <method v="2">\n'
        '      <option '
        'name="com.jetbrains.cidr.execution.CidrBuildBeforeRunTaskProvider'
        '$BuildBeforeRunTask" enabled="true" />\n'
        "    </method>\n"
        "  </configuration>\n"
        "</component>\n"
    )


def _render_clion_cmake_profiles(descriptors: list[dict[str, str]]) -> str:
    """The `<configuration .../>` lines for `.idea/cmake.xml`."""
    lines = [_CLION_FIRMWARE_PROFILE]
    for descriptor in descriptors:
        lines.append(
            '      <configuration '
            f'PROFILE_NAME="{descriptor["profile_name"]}" '
            'ENABLED="true" CONFIG_NAME="Debug" TOOLCHAIN_NAME="Default" '
            f'GENERATION_DIR="{descriptor["generation_dir"]}" '
            'GENERATION_OPTIONS="-DIDF_TARGET=linux '
            '-DCMAKE_BUILD_TYPE=Debug" />'
        )
    return "\n".join(lines)


def _update_clion_cmake_xml(
    content: str, descriptors: list[dict[str, str]]
) -> str:
    """Replace the `<configurations>` block with generated profiles.

    Preserves the surrounding file (header comment, component/project tags).
    """
    profiles = _render_clion_cmake_profiles(descriptors)
    replacement = (
        "    <configurations>\n"
        f"{profiles}\n"
        "    </configurations>"
    )
    pattern = re.compile(
        r"    <configurations>.*?    </configurations>", re.DOTALL
    )
    if not pattern.search(content):
        raise ValueError("cmake.xml has no <configurations> block")
    return pattern.sub(replacement, content, count=1)


def cmd_clion_run_configs() -> int:
    """Generate CLion CMake profiles and Linux ELF run/debug configurations.

    Discovers every `linux`-target ESP-IDF project in the repository and writes
    the matching `.idea/cmake.xml` linux profiles and
    `.idea/runConfigurations/Linux_ELF__*_container.xml` run/debug configs.
    Existing shared build/flash/menuconfig/etc. run configurations are left
    untouched. Stale generated Linux ELF configs for projects that no longer
    exist are removed.
    """
    descriptors = _clion_linux_descriptors()
    if not descriptors:
        print(
            "clion-run-configs: no linux executable projects found.",
            file=sys.stderr,
        )
        return 1

    run_dir = REPO_ROOT / ".idea" / "runConfigurations"
    run_dir.mkdir(parents=True, exist_ok=True)

    wanted = set()
    for descriptor in descriptors:
        filename = f'Linux_ELF__{descriptor["slug"]}__container.xml'
        wanted.add(filename)
        path = run_dir / filename
        path.write_text(
            _render_linux_run_configuration(descriptor), encoding="utf-8"
        )

    removed = 0
    for existing in sorted(run_dir.glob("Linux_ELF__*__container.xml")):
        if existing.name not in wanted:
            existing.unlink()
            removed += 1

    cmake_xml = REPO_ROOT / ".idea" / "cmake.xml"
    cmake_content = _read_text(cmake_xml)
    if cmake_content is None:
        print(
            "clion-run-configs: .idea/cmake.xml is missing.",
            file=sys.stderr,
        )
        return 1
    try:
        updated = _update_clion_cmake_xml(cmake_content, descriptors)
    except ValueError as error:
        print(f"clion-run-configs: {error}", file=sys.stderr)
        return 1
    cmake_xml.write_text(updated, encoding="utf-8")

    print(
        "clion-run-configs: wrote "
        f"{len(descriptors)} Linux ELF run/debug configuration(s) and "
        f"updated .idea/cmake.xml"
        + (f" (removed {removed} stale)" if removed else "")
        + "."
    )
    for descriptor in descriptors:
        print(f"  - {descriptor['display_name']} ({descriptor['rel_dir']})")
    return 0


def cmd_complete() -> int:
    """Print the command names matching a prefix (used by shell completion).

    Internal helper: the bash completion script calls this to list the
    subcommands that start with the word being completed. Returns 0 even when
    nothing matches (an empty candidate list is not an error for completion).
    """
    prefix = sys.argv[2] if len(sys.argv) > 2 else ""
    for name in COMMANDS:
        # Don't advertise the internal completion helper itself.
        if name == "complete":
            continue
        if name.startswith(prefix):
            print(name)
    return 0


def _staged_files(
    directories: list[Path] | None = None, root: Path = REPO_ROOT
) -> list[str]:
    """Staged, added/copied/modified C/C++ files under `directories`.

    With no `directories` the historical `ROOTS` trees are scanned. Deleted
    files are skipped (`--diff-filter=ACMR`) and vendored paths are dropped.
    `root` is a parameter so the directory filter can be unit-tested against a
    scratch repository.
    """
    pathspecs = (
        tuple(_repo_relative(directory, root) for directory in directories)
        if directories else ROOTS
    )
    out = subprocess.run(
        (
            "git", "diff", "--cached", "--name-only", "--diff-filter=ACMR",
            "--", *pathspecs,
        ),
        cwd=root,
        capture_output=True,
        text=True,
        check=True,
    ).stdout
    return [
        line for line in out.splitlines()
        if line.endswith(EXTENSIONS) and not _is_vendored_path(line)
    ]


def _staged_blob(path: str, repo: Path = REPO_ROOT) -> bytes:
    """The exact content of `path` in the index (what a commit would record).

    `_staged_files()` only yields added/copied/modified/renamed paths, so the
    path is always in the index and `check=True` cannot fire in practice.
    """
    return subprocess.run(
        ("git", "show", f":{path}"),
        cwd=repo,
        check=True,
        capture_output=True,
    ).stdout


def _style_blob(path: str, repo: Path = REPO_ROOT) -> bytes | None:
    """Content of a clang-format style file, or None if it is being removed.

    Tracked style files are read from the index, so the temp tree mirrors the
    commit exactly rather than the working copy. A style file that is *not*
    tracked still counts: `format` honors it in the working tree, and this
    check must agree with `format` instead of silently falling back to the root
    style. One staged for deletion is not part of the commit, so it is skipped.
    """
    staged = subprocess.run(
        ("git", "show", f":{path}"),
        cwd=repo,
        capture_output=True,
    )
    if staged.returncode == 0:
        return staged.stdout
    deleted = subprocess.run(
        ("git", "diff", "--cached", "--name-only", "--diff-filter=D",
         "--", path),
        cwd=repo,
        capture_output=True,
        text=True,
        check=True,
    ).stdout
    if deleted.strip():
        return None
    return (repo / path).read_bytes()


def _style_files_for(files: list[str], root: Path = REPO_ROOT) -> list[str]:
    """Repo-relative clang-format style files that apply to `files`.

    clang-format discovers style by searching *upward* from each source file
    and stops at the first one it finds, so only the ancestors of these files
    can contribute. Walking exactly those directories keeps the search cheap
    and avoids copying the style files of unrelated trees.

    `root` is a parameter so the discovery can be unit-tested against a scratch
    tree without touching the real repository.
    """
    found = set()
    for f in files:
        for directory in Path(f).parents:
            for name in STYLE_FILE_NAMES:
                candidate = directory / name
                if (root / candidate).is_file():
                    found.add(candidate.as_posix())
    return sorted(found)


def _materialize_staged(
    files: list[str], tmp_dir: Path, repo: Path = REPO_ROOT
) -> None:
    """Write the staged (index) version of each file into a temp tree.

    We check exactly what is being committed rather than the working copy.

    The clang-format style files that apply to these files are written at the
    same relative paths, because clang-format finds style by searching upward
    from each source file. Copying only the root `.clang-format` used to hide a
    *nested* one (say `DisableFormat: true` over vendored code), so the check
    reformatted those files against the root style and rejected commits that
    `format` leaves untouched. Copying beats bind-mounting a single file, which
    fails when the parent is mounted read-only.

    `repo` is a parameter so this can be exercised against a scratch repo.
    """
    contents = [(f, _staged_blob(f, repo)) for f in files]
    for style in _style_files_for(files, repo):
        blob = _style_blob(style, repo)
        if blob is not None:
            contents.append((style, blob))
    for path, blob in contents:
        dest = tmp_dir / path
        dest.parent.mkdir(parents=True, exist_ok=True)
        dest.write_bytes(blob)


def _run_staged_format_check(tmp_dir: Path, files: list[str]) -> int:
    """Run clang-format on the staged copies inside `tmp_dir`.

    The paths stay repo-relative and `cwd` is the temp tree, so clang-format
    resolves each file's style from the copies `_materialize_staged` wrote.
    """
    return run(("clang-format", *FORMAT_ARGS, *files), cwd=tmp_dir)


def cmd_format_check_staged(arguments: list[str]) -> int:
    """Check only the staged C/C++ content (used by the pre-commit hook).

    Unlike `format-check`, this inspects the *index* version of the staged
    files, so it never complains about unrelated working-tree edits and always
    validates exactly what is about to be committed. A directory argument
    narrows the check to the staged files under it; with none it starts from
    the current directory (the repository root for the git hook).
    """
    directories = _format_directories(arguments)
    if directories is None:
        return 1
    change_to_container(_project_container_arguments(
        "format-check-staged", directories))

    files = _staged_files(directories)
    if not files:
        return 0

    with tempfile.TemporaryDirectory() as tmp:
        tmp_dir = Path(tmp)
        _materialize_staged(files, tmp_dir)
        rc = _run_staged_format_check(tmp_dir, files)

    if rc == 0:
        return 0

    print("", file=sys.stderr)
    print(
        "format-check-staged: clang-format check FAILED — these staged files "
        "need formatting:",
        file=sys.stderr,
    )
    print("", file=sys.stderr)
    print("Fix with:  scripts/dev.py format", file=sys.stderr)
    print(
        "Or bypass (not recommended):  git commit --no-verify",
        file=sys.stderr,
    )
    return 1


COMMANDS = {
    "pull": cmd_pull,
    "image": cmd_image,
    "up": cmd_up,
    "down": cmd_down,
    "submodules": cmd_submodules,
    "build": cmd_build,
    "build-test-espidf-cpp": cmd_build_test_espidf_cpp,
    "flash": cmd_flash,
    "monitor": cmd_monitor,
    "menuconfig": cmd_menuconfig,
    "test-linux": cmd_test_linux,
    "test-esp32": cmd_test_esp32,
    "self-check": cmd_self_check,
    "format": cmd_format,
    "format-check": cmd_format_check,
    "format-check-staged": cmd_format_check_staged,
    "pycodestyle": cmd_pycodestyle,
    "clang-tidy": cmd_clang_tidy,
    "install-hooks": cmd_install_hooks,
    "shell": cmd_shell,
    "ssh": cmd_ssh,
    "clion-archive": cmd_clion_archive,
    "clion-compdb": cmd_clion_compdb,
    "clion-manual-test": cmd_clion_manual_test,
    "clion-run-configs": cmd_clion_run_configs,
    "complete": cmd_complete,
}


def _build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        prog="dev.py",
        description="Container task runner for the ESP32 thermometer logger.",
    )
    subparsers = parser.add_subparsers(dest="command", metavar="command")
    subparsers.required = True

    # Preserve the historical one-line order in --help output.
    for name, handler in COMMANDS.items():
        # `complete` is an internal helper invoked by the bash completion
        # script; it is handled before argparse and hidden from --help.
        if name == "complete":
            continue
        subparser = subparsers.add_parser(
            name,
            help=(handler.__doc__ or "").strip().splitlines()[0],
            description=handler.__doc__,
        )
        subparser.set_defaults(handler=handler)

    # `shell` accepts arbitrary trailing arguments to run in the container
    # shell. REMAINDER captures them verbatim (including flags and shell
    # metacharacters) so `shell ls -1d /` and `shell "echo hi | wc -l"` work.
    # The `-C/--directory` option must appear before the command (options are
    # parsed up to the first positional token, which starts the REMAINDER).
    # `build` takes optional directories (default: every project).
    build_parser = subparsers.choices["build"]
    build_parser.add_argument(
        "--analyzer",
        type=_bool_value,
        default=False,
        metavar="BOOL",
        help="enable GCC's static analyzer (-fanalyzer); off by default. Use "
             "`--analyzer` for on, or `--analyzer=false` for off (the value "
             "must be attached with `=`, so a following directory is not "
             "consumed)",
    )
    build_parser.add_argument(
        "directories",
        nargs="*",
        metavar="DIRECTORY",
        help="directories (every ESP-IDF project in or under one is built); "
             "omit for the current directory",
    )

    # `test-linux` runs the host suites it discovers; `test-esp32` builds and
    # runs the on-target ones. Both take optional directories.
    test_linux_parser = subparsers.choices["test-linux"]
    test_linux_parser.add_argument(
        "directories",
        nargs="*",
        metavar="DIRECTORY",
        help="directories (every host (`linux`) test project in or under one "
             "is built and run); omit for the current directory",
    )
    test_esp32_parser = subparsers.choices["test-esp32"]
    test_esp32_parser.add_argument(
        "directories",
        nargs="*",
        metavar="DIRECTORY",
        help="directories (every on-target ESP-IDF test project in or under "
             "one is built and run on the board); omit for the current "
             "directory",
    )

    # `clang-tidy` analyzes every project by default; naming directories
    # restricts the run (handy while working on one area). A directory covers
    # every C++ source below it. `--fix` forwards to clang-tidy, which then
    # rewrites the sources in place.
    tidy_parser = subparsers.choices["clang-tidy"]
    tidy_parser.add_argument(
        "--fix",
        action="store_true",
        help="apply clang-tidy's suggested fixes in place; re-run "
             "`format` afterwards",
    )
    tidy_parser.add_argument(
        "directories",
        nargs="*",
        metavar="DIRECTORY",
        help="directories (every C++ source under one is analyzed); omit for "
             "the current directory",
    )

    # The format commands take optional directories, defaulting to the current
    # directory (see `_format_directories`).
    for name in ("format", "format-check", "format-check-staged"):
        subparsers.choices[name].add_argument(
            "directories",
            nargs="*",
            metavar="DIRECTORY",
            help="directories (every C/C++ source under one is used); omit "
                 "for the current directory",
        )

    shell_parser = subparsers.choices["shell"]
    shell_parser.add_argument(
        "-C",
        "--directory",
        metavar="DIR",
        default=None,
        help="change into DIR (a path in the container) before running the "
             "command; a relative DIR is resolved under /project",
    )
    shell_parser.add_argument(
        "args",
        nargs=argparse.REMAINDER,
        metavar="ARG",
        help="command (and arguments) to run in the container shell",
    )

    ssh_parser = subparsers.choices["ssh"]
    ssh_parser.add_argument(
        "args",
        nargs=argparse.REMAINDER,
        metavar="ARG",
        help="command (and arguments) to run over SSH; omit for an "
             "interactive login shell",
    )

    clion_parser = subparsers.choices["clion-archive"]
    clion_parser.add_argument(
        "archive",
        nargs="?",
        default=None,
        metavar="ARCHIVE",
        help="CLion-*.tar.gz to copy; omit to use the only one in ~/Downloads",
    )

    return parser


def main(argv: list[str]) -> int:
    # `complete` takes a trailing prefix argument (e.g. `dev.py complete form`)
    # that argparse would reject, so handle it before building the parser.
    if len(argv) > 1 and argv[1] == "complete":
        return cmd_complete()

    # Convert the path relative to REPO_ROOT since the commands
    # are executed with cwd as REPO_ROOT.  Needed when we
    # re-execute dev.py inside the container.
    prog = Path(argv[0]).resolve()
    relative_prog = prog.relative_to(REPO_ROOT)
    sys.argv[0] = str(relative_prog)

    parser = _build_parser()
    args = parser.parse_args(_normalize_analyzer_argv(argv[1:]))
    if args.command == "shell":
        return cmd_shell(args.args, args.directory)
    if args.command == "ssh":
        return cmd_ssh(args.args)
    if args.command == "clion-archive":
        return cmd_clion_archive(args.archive)
    if args.command == "build":
        return cmd_build(args.directories, analyzer=args.analyzer)
    if args.command == "test-linux":
        return cmd_test_linux(args.directories)
    if args.command == "test-esp32":
        return cmd_test_esp32(args.directories)
    if args.command == "clang-tidy":
        directories = _parse_tidy_scopes(args.directories)
        if directories is None:
            return 1
        return cmd_clang_tidy(directories, fix=args.fix)
    if args.command == "format":
        return cmd_format(args.directories)
    if args.command == "format-check":
        return cmd_format_check(args.directories)
    if args.command == "format-check-staged":
        return cmd_format_check_staged(args.directories)
    return args.handler()


if __name__ == "__main__":
    sys.exit(main(sys.argv))

# CODE INSPECTION FINDING (scripts/dev.py:2790-2822)
# _opened_project_log_check() returns PROJECT_OPEN_PENDING on exec/read errors.
# wait_for_remote_project() only surfaces non-pending details, so a persistent
# read error polls silently until timeout. Consider surfacing pending error
# details or distinguishing "log not written yet" from "cannot read log".
# _remote_project_dir_log_check() hard-fails on read errors, which is clearer
# for its non-polling check.
#
# CODE INSPECTION FINDING (scripts/dev.py:2738-2822)
# _backend_download_log_check, _opened_project_log_check, and
# _remote_project_dir_log_check share compose-exec/cat/error-handling
# boilerplate. A small helper could reduce drift while keeping
# pending-vs-hard-fail behavior explicit.
