#!/usr/bin/env python3
#
# ESP32 Solar-Powered Thermometer Logger — container task runner.
#
# Every build/flash/test/lint task runs inside the ESP-IDF dev container so the
# host stays clean and the toolchain is reproducible. This wrapper is the single
# entry point used by the CLion run configurations (see
# .idea/runConfigurations/*.xml) and the AGENTS.md workflow, so `docker compose`
# specifics live in one place.
#
# Usage:
#   scripts/dev.py [pull|image|submodules|build|build-espidf-cpp-test|flash|\
#                   monitor|menuconfig|test|test-linux|self-check|format|\
#                   format-check|format-check-staged|clang-tidy|\
#                   install-hooks|shell|cn]
#   scripts/dev.py --help        # full help, including per-command details
#
# Environment:
#   ESP32_PORT   Serial device for flash/monitor (optional)
#                docker-compose.yml will pass into the container the value 
#                on the host.
#   GH_TOKEN     GitHub token for `gh` inside the container (optional;
#                GITHUB_TOKEN is accepted too). docker-compose.yml passes
#                whichever is set on the host, and `gh` prefers it over any
#                stored credential. See Developer.md, "GitHub CLI (`gh`) in
#                the container".
#   DEEPSEEK_API_KEY
#                Secret resolved by the Continue CLI (`cn`) config
#                (`${{ secrets.DEEPSEEK_API_KEY }}`). docker-compose.yml
#                passes it from the host environment into the container, so
#                `scripts/dev.py cn` needs no config change on the host.
#
"""Container task runner for the ESP32 thermometer logger."""

from __future__ import annotations

import argparse
import json
import os
from pathlib import Path
import re
import shlex
import subprocess
import sys
import tempfile

# Always operate from the repository root, regardless of the caller's cwd.
REPO_ROOT = Path(__file__).resolve().parent.parent

# Directories whose C/C++ sources are checked/formatted by clang-format. The
# file list is discovered via git (`source_files`), and deliberately covers
# untracked files as well as tracked ones, so a source that exists but has not
# been `git add`ed is still formatted. Discovering the files through git keeps
# the walk to source trees (no build output, no missing-glob errors). Keep this
# in sync with the pre-commit hook (.githooks/pre-commit).
ROOTS = (
    "mcgowan",
    "components",
    "test",
    "espidf-cpp-test",
    "donny",
    "scripts",
)
EXTENSIONS = (".cpp", ".hpp", ".h", ".cc", ".cxx")

# Git submodules holding vendored third-party code the build needs. A bare
# clone leaves each submodule directory empty, so the build/test/lint commands
# fetch them first, and `scripts/dev.py submodules` (CI step) does so explicitly.
#
# `third_party/sml` is boost-ext/sml (https://github.com/boost-ext/sml),
# pinned to tag **v1.2.0** via the gitlink recorded in .gitmodules. The
# `components/sml` component exposes its headers to `logic`, which uses it for
# the application state machines. Its sources are NOT ours: they sit outside
# ROOTS (so clang-format never touches them) and are excluded from clang-tidy
# (see `_is_vendored`).
SUBMODULES = ("third_party/sml",)

# A file whose presence proves a submodule is actually checked out. Its absence
# means the gitlink was not fetched (fresh clone, or `--no-recurse-submodules`).
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
DEV_IMAGE = "marcpawl/esp-idf:20261109"
LOCAL_IMAGE = "esp32-sensors-dev:local"

# User baked into DEV_IMAGE, and where ccache lives under a writable HOME. A
# container run as any *other* uid (a host user, or a CI runner whose uid is
# 1001) has no passwd entry in the image, so `$HOME` would be unusable and
# ccache would fail with "ccache: error: Permission denied". `compose_run`
# therefore pins HOME (and CCACHE_DIR) explicitly.
CONTAINER_UID = 1000
HOME_CACHE_DIR = ".cache/ccache"

FORMAT_ARGS = ("--dry-run", "--Werror")

# The repo root is not an ESP-IDF project; it aggregates the subprojects
# (each a standalone ESP-IDF project with its own build directory):
#
#   mcgowan/  ESP32-S3 firmware (hal_board) -> mcgowan/build_esp32s3/mcgowan.bin
#   test/     host unit tests (linux target)      -> test/build_linux
#   espidf-cpp-test/
#             host unit tests for components/espidf_cpp (linux target)
#                                                -> espidf-cpp-test/build_linux
#   donny/    linux-emulated application (hal_stub, linux target)
#                                                -> donny/build_linux
#
# Build directories are named after the ESP-IDF target so the esp32s3 and
# linux builds stay separate.
FIRMWARE_PROJECT = "mcgowan"
TEST_PROJECT = "test"
ESPIDF_CPP_TEST_PROJECT = "espidf-cpp-test"
DONNY_PROJECT = "donny"
FW_BUILD_DIR = "build_esp32s3"
TEST_BUILD_DIR = "build_linux"
ROOT_BUILD_DIR = "build_root"

# The firmware app binary (named after the mcgowan project).
FW_BINARY = "mcgowan.bin"

# The host-test projects (linux target) and the ELF each one produces (named
# after its ESP-IDF project). Each is pinned to `linux`, built, then run.
HOST_TEST_PROJECTS = (
    (TEST_PROJECT, "esp32_sensors_tests.elf"),
    (ESPIDF_CPP_TEST_PROJECT, "esp32_sensors_espidf_cpp_tests.elf"),
)

# Subprojects that build for the ESP-IDF `linux` target (into build_linux).
# `donny` is an application rather than a test suite, so it is built but is not
# run by `test-linux` (see HOST_TEST_PROJECTS).
LINUX_PROJECTS = (
    TEST_PROJECT,
    ESPIDF_CPP_TEST_PROJECT,
    DONNY_PROJECT,
)

SUBPROJECTS = (
    FIRMWARE_PROJECT,
    TEST_PROJECT,
    ESPIDF_CPP_TEST_PROJECT,
    DONNY_PROJECT,
)

# Host-side unit tests for this script itself (stdlib `unittest`), run by
# `scripts/dev.py self-check`.
SELF_CHECK_MODULE = "scripts.test_dev"

# clang-tidy: the ESP-IDF projects whose compile databases seed the analysis,
# mapped to their build directory. clang-tidy consumes a compilation database,
# so these builds must have run first (`scripts/dev.py build`).
TIDY_PROJECTS = (
    (FIRMWARE_PROJECT, FW_BUILD_DIR),
    (TEST_PROJECT, TEST_BUILD_DIR),
    (ESPIDF_CPP_TEST_PROJECT, TEST_BUILD_DIR),
    (DONNY_PROJECT, TEST_BUILD_DIR),
)

# Source extensions clang-tidy analyzes. The compile database lists every
# translation unit, including the generated C/assembly ESP-IDF compiles; only
# C++ sources are ours to check.
TIDY_EXTENSIONS = (".cpp", ".cc", ".cxx")

# GCC driver flags clang's driver rejects outright, or accepts only to warn
# "argument unused" (which `--warnings-as-errors` would turn into a failure).
# ESP-IDF passes them to the xtensa GCC for code-size reasons; they are
# meaningless to clang-tidy, which only parses (never code-generates).
TIDY_DROP_FLAGS = frozenset({
    "-c",
    "-fno-jump-tables",
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
)


def is_in_container():
    return os.environ.get("DOCKER_IMAGE") == "esp32_sensors"

def split_list(items, max_size):
    # Step through the list from 0 to n, skipping by max_size each time
    return [items[i : i + max_size] for i in range(0, len(items), max_size)]

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

    The pathspec is scoped to `ROOTS` rather than the whole repository, so
    `third_party/` (vendored, must stay byte-identical) is never picked up.
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
        if path.endswith(EXTENSIONS) and (root / path).is_file()
    })


def source_files() -> list[str]:
    """Every C/C++ source under `ROOTS` that exists, tracked or untracked.

    This is the file list `format` and `format-check` operate on. It includes
    files that have not been `git add`ed yet, so formatting a new source does
    not depend on staging it first; ignored files (build output) are still
    excluded by `--exclude-standard`.
    """
    try:
        result = subprocess.run(
            _source_file_command(),
            cwd=REPO_ROOT,
            capture_output=True,
            text=True,
            check=True,
        )
    except subprocess.CalledProcessError as e:
        print(f"Git command failed with exit code {e.returncode}", file=sys.stderr)
        print(f"Error output: {e.stderr}", file=sys.stderr)
        raise
    except FileNotFoundError:
        print("Error: 'git' command-line tool not found on this system.", file=sys.stderr)
        raise

    return _select_sources(result.stdout.splitlines())


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
    command = ["docker", "compose", "run",
               "--user", container_user,
               *_container_environment(container_user),
               "--rm", "dev"]
    command.extend(args)
    return run(command)


def change_to_container():
    """If we are not in the container, then
       reexecute dev.py command in the container.

       Otherwise do noting.
    """
    if not is_in_container():
        rc = compose_run(sys.argv)
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
        path for path, marker in markers.items() if not (root / marker).is_file()
    ]


def _submodule_update_command(paths: list[str]) -> list[str]:
    """The `git submodule update` argv that fetches `paths`. Pure (testable).

    `--init` registers the submodule in `.git/config` and `--recursive` covers
    any nested submodules (boost-ext/sml has none today, but the flag keeps a
    future one from silently staying empty). Only the named paths are touched,
    so an unrelated submodule is left exactly as the caller left it.
    """
    return ["git", "submodule", "update", "--init", "--recursive", "--", *paths]


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
        f"Fetching {len(missing)} missing git submodule(s): {' '.join(missing)}"
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


def cmd_image() -> int:
    """Build the dev image locally from the Dockerfile (not needed normally)."""
    if is_in_container():
        print("Image cannot be built while in container", file=sys.stderr)
        return 1
    return run(["docker", "build", "-t", LOCAL_IMAGE, "."])


def _idf(project: str, build_dir: str, *args: str) -> list[str]:
    """`idf.py` argv for a subproject: run in its directory with its own build.

    Pure (no I/O), so it is unit-testable. `project` is a subproject directory
    relative to the repo root; the build directory lives inside it.
    """
    return ["idf.py", "-C", project, "-B", f"{project}/{build_dir}", *args]


def _parse_subprojects(args: list[str]) -> list[str] | None:
    """Parse `build`'s optional subproject arguments.

    No arguments means every subproject. Unknown names print an error and
    return None so the caller can exit non-zero.
    """
    if not args:
        return list(SUBPROJECTS)
    unknown = [arg for arg in args if arg not in SUBPROJECTS]
    if unknown:
        print(
            f"Unknown subproject(s): {' '.join(unknown)}. "
            f"Known: {' '.join(SUBPROJECTS)}",
            file=sys.stderr,
        )
        return None
    return args


def _root_build_commands(subprojects: list[str]) -> list[list[str]]:
    """Configure and build the root aggregator project. Pure (testable).

    The root project is plain CMake (not ESP-IDF); ``SUBPROJECTS`` selects
    which of its ``<subproject>_build`` targets are built. It is passed on the
    configure command line rather than cached so the selection never goes
    stale. Returns the configure command followed by the build command.
    """
    return [
        [
            "cmake",
            "-S", ".",
            "-B", ROOT_BUILD_DIR,
            f"-DSUBPROJECTS={';'.join(subprojects)}",
        ],
        ["cmake", "--build", ROOT_BUILD_DIR],
    ]


def _build_subprojects(subprojects: list[str]) -> int:
    """Build the named subprojects in order. No container hop."""
    if subprojects == list(SUBPROJECTS):
        # The whole tree: let the root aggregator project drive every
        # subproject (one CMake build over all of them).
        for cmd in _root_build_commands(subprojects):
            rc = run(cmd)
            if rc != 0:
                return rc
        return 0
    for project in subprojects:
        rc = _build_project(project)
        if rc != 0:
            return rc
    return 0


def _build_project(project: str) -> int:
    """Build one subproject with its own target and build directory.

    The host-test projects (`test`, `espidf-cpp-test`) are pinned to the
    `linux` target and build into `build_linux`; the firmware builds for
    ESP32-S3 into `build_esp32s3`.
    """
    if project in LINUX_PROJECTS:
        rc = run(_idf(project, TEST_BUILD_DIR, "--preview", "set-target",
                      "linux"))
        if rc != 0:
            return rc
        return run(_idf(project, TEST_BUILD_DIR, "build"))
    return run(_idf(project, FW_BUILD_DIR, "build"))


def cmd_build(args: list[str]) -> int:
    """Build the firmware; with no argument also the other subprojects.

    `build`            → every subproject (firmware, host tests)
    `build mcgowan`    → just the ESP32-S3 firmware (mcgowan/build_esp32s3)
    `build test`       → any of: mcgowan test espidf-cpp-test donny
    """
    change_to_container()
    rc = _ensure_submodules()
    if rc != 0:
        return rc
    subprojects = _parse_subprojects(args)
    if subprojects is None:
        return 1
    return _build_subprojects(subprojects)


def cmd_build_espidf_cpp_test() -> int:
    """Build the espidf_cpp unit tests for the ESP-IDF `linux` target."""
    change_to_container()
    rc = _ensure_submodules()
    if rc != 0:
        return rc
    return _build_project(ESPIDF_CPP_TEST_PROJECT)


def cmd_flash() -> int:
    """Flash then attach the serial monitor; exit it with Ctrl+]."""
    change_to_container()
    # No need to pass in the port since it is either in the
    # container environment of idf.py will hunt for the board
    # across all the tty devices.
    return run(_idf(FIRMWARE_PROJECT, FW_BUILD_DIR, "flash", "monitor"))


def cmd_monitor() -> int:
    """Attach the serial monitor; exit it with Ctrl+]."""
    change_to_container()
    return run(_idf(FIRMWARE_PROJECT, FW_BUILD_DIR, "monitor"))


def cmd_menuconfig() -> int:
    """Interactive Kconfig UI; edits sdkconfig (generated from defaults)."""
    change_to_container()
    return run(_idf(FIRMWARE_PROJECT, FW_BUILD_DIR, "menuconfig"))


def _run_host_tests() -> int:
    """Build and run every host test suite (ESP-IDF `linux` target).

    No container hop; assumes it is already running inside the dev container.
    Each project in `HOST_TEST_PROJECTS` is pinned to `linux`, built, and its
    ELF run; the first failing suite stops the run with a non-zero status.
    """
    rc = _ensure_submodules()
    if rc != 0:
        return rc
    for project, elf in HOST_TEST_PROJECTS:
        for cmd in (
            _idf(project, TEST_BUILD_DIR, "--preview", "set-target", "linux"),
            _idf(project, TEST_BUILD_DIR, "build"),
            [f"./{project}/{TEST_BUILD_DIR}/{elf}"],
        ):
            rc = run(cmd)
            if rc != 0:
                print(f"FAILED: host tests '{project}'", file=sys.stderr)
                return 1

    print("SUCCESS: host tests passed")
    return 0


def cmd_test_linux() -> int:
    """Host-based Unity tests on the ESP-IDF `linux` target."""
    change_to_container()
    return _run_host_tests()


def cmd_test() -> int:
    """Run the host unit test suites (alias of `test-linux`)."""
    change_to_container()
    return _run_host_tests()


def cmd_self_check() -> int:
    """Run the host-side unit tests for this script (stdlib `unittest`).

    Exercises `dev.py`'s own pure helpers (e.g. the command line `shell`
    builds) with the standard-library test runner, so no Docker hop, container,
    or third-party dependency is needed. Works on the host and inside the
    container alike.
    """
    return run(["python3", "-m", "unittest", "-v", SELF_CHECK_MODULE])


def cmd_format() -> int:
    """Apply the committed .clang-format (Google base, 4-space, 80 cols)."""
    change_to_container()

    for file in source_files():
        cmd = [ "clang-format", "-i", file ]
        rc = run(cmd)
        if rc != 0:
            print(f"Format failed: {file}", file=sys.stderr)
            return 1
    return 0


def cmd_format_check() -> int:
    """CI-friendly check; non-zero exit if anything is unformatted."""
    change_to_container()

    for file in source_files():
        cmd = [ "clang-format", "--dry-run", "--Werror", file ]
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
    `-fno-tree-switch-conversion`, `-fno-jump-tables`, `-freorder-blocks` and
    `-specs=...` are GCC-only, and `-fmacro-prefix-map=` is not a clang flag
    spelling clang accepts. Left in, they surface as `clang-diagnostic-error`s
    (or "argument unused" warnings, which `--warnings-as-errors` promotes).

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


def _compiler_system_include_flags(compiler: str, args: list[str]) -> list[str]:
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
    compiler restores it — `xtensa-esp-elf` for the firmware, `x86_64-linux-gnu`
    for the `linux`-target host projects.

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

    A shared source (`components/logic/src/payload.cpp`, most of the HAL) is
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


def _tidy_units(projects: list[str]) -> list[dict] | None:
    """The C++ translation units of `projects`, or None if a build is missing.

    Seeds the analysis from each project's compile database, then reduces the
    union to one entry per source file (see `_unique_tidy_units`).
    """
    compilations: list[dict] = []
    for project in projects:
        build_dir = dict(TIDY_PROJECTS)[project]
        database = REPO_ROOT / project / build_dir / "compile_commands.json"
        if not database.is_file():
            print(
                f"clang-tidy: '{project}' has no "
                f"{database.relative_to(REPO_ROOT)}; "
                f"run `scripts/dev.py build {project}` first.",
                file=sys.stderr,
            )
            return None
        compilations.extend(json.loads(database.read_text()))
    return _unique_tidy_units(compilations)


def cmd_clang_tidy(projects: list[str]) -> int:
    """Static analysis (clang-tidy) over the C/C++ sources. No container hop.

    Imports the ESP-IDF compile databases so clang-tidy sees the exact flags,
    defines, and include paths each file is really built with, including the
    `hal_board`/`hal_stub` split that differs per project.

    Nothing is auto-built: a project with no compile database is reported and
    the run fails, so CI cannot quietly analyze stale or partial input. Use
    `scripts/dev.py build` first.

    Uses clang-tidy's default check set (`clang-diagnostic-*`,
    `clang-analyzer-*`) — the repository commits no `.clang-tidy` — and reports
    only diagnostics in this repository's own headers, not ESP-IDF's or
    libstdc++'s. Any finding is an error (`--warnings-as-errors=*`).
    """
    change_to_container()

    rc = _ensure_submodules()
    if rc != 0:
        return rc
    units = _tidy_units(projects)
    if units is None:
        return 1
    if not units:
        print("clang-tidy: no C++ sources to analyze", file=sys.stderr)
        return 1

    with tempfile.TemporaryDirectory() as tmp:
        tmp_dir = Path(tmp)
        _tidy_database(units, tmp_dir / "compile_commands.json")
        print(f"clang-tidy: analyzing {len(units)} C++ sources")
        return run([
            "clang-tidy",
            "-p", str(tmp_dir),
            "--header-filter", _tidy_header_filter(),
            "--warnings-as-errors=*",
            "--quiet",
            *[unit["file"] for unit in units],
        ])


def _tidy_header_filter() -> str:
    """Only report diagnostics in *our* headers, not ESP-IDF's or libstdc++'s.

    clang-tidy names a diagnostic by the file it lands in, and a template error
    often lands in a system header. Restricting to the repository's own
    directories keeps the signal on code we can fix.
    """
    roots = "|".join(re.escape(root) for root in ROOTS if root != "scripts")
    return rf"^{re.escape(str(REPO_ROOT))}/({roots})/"


def cmd_install_hooks() -> int:
    """Activate the version-controlled git hooks (.githooks/pre-commit).

    The hook rejects commits whose staged C/C++ files are not
    clang-formatted.
    """
    rc = run(["git", "-C", str(REPO_ROOT), "config", "core.hooksPath", ".githooks"])
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
    the project mount (`test` -> /project/test), an absolute path is used as-is:

        scripts/dev.py shell -C test ls -1
        scripts/dev.py shell -C /project/test idf.py build

    """
    change_to_container()
    return run(_build_shell_command(extra, directory))


# The Continue CLI (`cn`) configuration file, relative to the container's
# `$HOME`. `docker-compose.yml` bind-mounts the host's `~/.continue` at both
# possible container homes, so this is the host's config seen from inside the
# container. It is passed explicitly with `--config` because cn's own default
# location is not reliably used (cn falls back to a remote default config when
# it looks in the wrong place), and the config is the only thing that names the
# DeepSeek models and the `secrets.DEEPSEEK_API_KEY` they authenticate with.
CN_CONFIG_PATH = ".continue/config.yaml"


def _cn_command(extra: list[str], home: str | None = None) -> list[str]:
    """The `cn` argv `cmd_cn` runs. Pure (testable).

    `--config` names the file explicitly, ahead of the caller's arguments, so a
    later `--config` on the command line still wins (last one wins) but the
    default is always the mounted `~/.continue/config.yaml` rather than
    whatever rc file cn decides to look for.

    `home` is the container's HOME, spelled by `_container_environment`, so the
    path is resolved for the uid the container actually runs as (uid 1000 keeps
    `/home/dev`, every other uid gets `/tmp`) — both locations are mounted.
    """
    config = str(Path(home or "/home/dev") / CN_CONFIG_PATH)
    return ["cn", "--config", config, *extra]


def cmd_cn(extra: list[str]) -> int:
    """Run the Continue CLI (`cn`) inside the container.

    Uses the host's Continue config, bind-mounted by `docker-compose.yml` at
    `$HOME/.continue` (see `CN_CONFIG_PATH`), and names it with `--config`
    because cn's default lookup is not reliable. The DeepSeek models that
    config declares authenticate with `${{ secrets.DEEPSEEK_API_KEY }}`, which
    cn resolves from the environment, so `DEEPSEEK_API_KEY` is passed through
    from the host by `docker-compose.yml`.

    Arguments after `cn` are passed to `cn` verbatim, so both the interactive
    agent and headless runs work:

        scripts/dev.py cn
        scripts/dev.py cn -p "summarize scripts/dev.py"
        scripts/dev.py cn --format json -p "list the files in components/"
    """
    change_to_container()
    return run(_cn_command(extra, _container_home(_container_user())))


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


def _staged_files() -> list[str]:
    """Staged, added/copied/modified C/C++ files (deleted files skipped)."""
    out = git(
        "diff",
        "--cached",
        "--name-only",
        "--diff-filter=ACMR",
        "--",
        *ROOTS,
    ).stdout
    return [line for line in out.splitlines() if line.endswith(EXTENSIONS)]


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


def cmd_format_check_staged() -> int:
    """Check only the staged C/C++ content (used by the pre-commit hook).

    Unlike `format-check`, this inspects the *index* version of the staged
    files, so it never complains about unrelated working-tree edits and always
    validates exactly what is about to be committed.
    """
    change_to_container()

    files = _staged_files()
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
    print("Or bypass (not recommended):  git commit --no-verify", file=sys.stderr)
    return 1


COMMANDS = {
    "pull": cmd_pull,
    "image": cmd_image,
    "submodules": cmd_submodules,
    "build": cmd_build,
    "build-espidf-cpp-test": cmd_build_espidf_cpp_test,
    "flash": cmd_flash,
    "monitor": cmd_monitor,
    "menuconfig": cmd_menuconfig,
    "test": cmd_test,
    "test-linux": cmd_test_linux,
    "self-check": cmd_self_check,
    "format": cmd_format,
    "format-check": cmd_format_check,
    "format-check-staged": cmd_format_check_staged,
    "clang-tidy": cmd_clang_tidy,
    "install-hooks": cmd_install_hooks,
    "shell": cmd_shell,
    "cn": cmd_cn,
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
    # `build` takes optional subproject names (default: all of them).
    build_parser = subparsers.choices["build"]
    build_parser.add_argument(
        "subprojects",
        nargs="*",
        metavar="SUBPROJECT",
        help=f"subprojects to build (any of: {' '.join(SUBPROJECTS)}); "
             "omit for all of them",
    )

    # `clang-tidy` analyzes all analyzed projects by default; naming some
    # restricts the run (handy while working on one subproject).
    tidy_parser = subparsers.choices["clang-tidy"]
    tidy_parser.add_argument(
        "projects",
        nargs="*",
        metavar="PROJECT",
        help=f"subprojects to analyze (any of: {' '.join(SUBPROJECTS)}); "
             "omit for all of them",
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

    # `cn` declares no arguments: `main` hands everything after it to `cn`
    # verbatim (see the dispatch there), because every one of those arguments
    # is a flag (`-p`, `--format`, `--model`, ...) that argparse would claim
    # or reject here. `cn --help` is also more useful than a restatement.

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

    # `cn` likewise: every argument after it belongs to `cn` (`-p`, `--format`,
    # `--model`, ...), and argparse would claim one (`--help`) or reject the
    # rest as unknown options. Handing them over verbatim is what makes
    # `dev.py cn -p "..."` and `dev.py cn --format json -p "..."` work. `cn`
    # is still in COMMANDS, so it is listed in `--help` and completed by the
    # bash completion script. This sits after the `sys.argv[0]` rewrite above
    # because `cmd_cn` re-execs `sys.argv` inside the container, where the
    # host's absolute script path does not exist (the repo is `/project`).
    if len(argv) > 1 and argv[1] == "cn":
        return cmd_cn(argv[2:])

    parser = _build_parser()
    args = parser.parse_args(argv[1:])
    if args.command == "shell":
        return cmd_shell(args.args, args.directory)
    if args.command == "build":
        return cmd_build(args.subprojects)
    if args.command == "clang-tidy":
        projects = _parse_subprojects(args.projects)
        if projects is None:
            return 1
        return cmd_clang_tidy(projects)
    return args.handler()


if __name__ == "__main__":
    sys.exit(main(sys.argv))
