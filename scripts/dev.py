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
#   scripts/dev.py [pull|image|build|build-espidf-cpp-test|flash|monitor|\
#                   menuconfig|test|test-linux|self-check|format|\
#                   format-check|format-check-staged|install-hooks|shell]
#   scripts/dev.py --help        # full help, including per-command details
#
# Environment:
#   ESP32_PORT   Serial device for flash/monitor (optional)
#                docker-compose.yml will pass into the container the value 
#                on the host.
#
"""Container task runner for the ESP32 thermometer logger."""

from __future__ import annotations

import argparse
import os
from pathlib import Path
import shutil
import shlex
import subprocess
import sys
import tempfile

# Always operate from the repository root, regardless of the caller's cwd.
REPO_ROOT = Path(__file__).resolve().parent.parent

# Directories whose C/C++ sources are checked/formatted by clang-format. The
# file list is discovered via git so it always matches the tracked files (no
# missing-glob errors). Keep this in sync with the pre-commit hook
# (.githooks/pre-commit).
ROOTS = (
    "mcgowan",
    "components",
    "test",
    "espidf-cpp-test",
    "donny",
    "scripts",
)
EXTENSIONS = (".cpp", ".hpp", ".h", ".cc", ".cxx")

# Docker dev image: the prebuilt ESP-IDF v6.1 image plus this project's tooling
# (gh, PyGithub, esp-clang). It is pinned in docker-compose.yml too; the tag is
# never moved, so update it in both places (test_dev.py asserts they agree).
# `LOCAL_IMAGE` is only what `image` (a local rebuild) produces, for trying out
# Dockerfile changes.
DEV_IMAGE = "marcpawl/esp-idf:20261025"
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


def is_in_container():
    return os.environ.get("DOCKER_IMAGE") == "esp32_sensors"

def split_list(items, max_size):
    # Step through the list from 0 to n, skipping by max_size each time
    return [items[i : i + max_size] for i in range(0, len(items), max_size)]

def source_files():
    try:
        # Run the git command safely
        # This matches tracked files in the directories listed in ROOTS.
        result = subprocess.run(
            ["git", "ls-files", *[f"{root}/*" for root in ROOTS]],
            capture_output=True,
            text=True,
            check=True
        )
    
        # Split the output into individual file paths and strip whitespace
        all_files = [line.strip() for line in result.stdout.splitlines() if line.strip()]
    
        # Filter the files using the EXTENSIONS tuple
        filtered_files = [file for file in all_files if file.endswith(EXTENSIONS)]
        # Skip tracked-but-deleted files: formatting cannot read them.
        return [file for file in filtered_files if (REPO_ROOT / file).is_file()]
    
    except subprocess.CalledProcessError as e:
        print(f"Git command failed with exit code {e.returncode}", file=sys.stderr)
        print(f"Error output: {e.stderr}", file=sys.stderr)
        raise
    except FileNotFoundError:
        print("Error: 'git' command-line tool not found on this system.", file=sys.stderr)
        raise
    

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
    return [
        "-e", "HOME=/tmp",
        "-e", f"CCACHE_DIR=/tmp/{HOME_CACHE_DIR}",
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
    subprojects = _parse_subprojects(args)
    if subprojects is None:
        return 1
    return _build_subprojects(subprojects)


def cmd_build_espidf_cpp_test() -> int:
    """Build the espidf_cpp unit tests for the ESP-IDF `linux` target."""
    change_to_container()
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


def _materialize_staged(files: list[str], tmp_dir: Path) -> None:
    """Write the staged (index) version of each file into a temp tree.

    We check exactly what is being committed rather than the working copy. The
    committed .clang-format is copied in too so clang-format discovers it (it
    searches upward from each source file). Copying beats bind-mounting a
    single file, which fails when the parent is mounted read-only.
    """
    for f in files:
        dest = tmp_dir / f
        dest.parent.mkdir(parents=True, exist_ok=True)
        blob = subprocess.run(
            ("git", "show", f":{f}"),
            cwd=REPO_ROOT,
            check=True,
            capture_output=True,
        ).stdout
        dest.write_bytes(blob)
    shutil.copyfile(REPO_ROOT / ".clang-format", tmp_dir / ".clang-format")


def _run_staged_format_check(tmp_dir: Path, files: list[str]) -> int:
    """Run clang-format --dry-run --Werror"""
    return run(
            ("clang-format", *FORMAT_ARGS, *files),
            cwd=tmp_dir,
        )

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
    "install-hooks": cmd_install_hooks,
    "shell": cmd_shell,
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
    args = parser.parse_args(argv[1:])
    if args.command == "shell":
        return cmd_shell(args.args, args.directory)
    if args.command == "build":
        return cmd_build(args.subprojects)
    return args.handler()


if __name__ == "__main__":
    sys.exit(main(sys.argv))
