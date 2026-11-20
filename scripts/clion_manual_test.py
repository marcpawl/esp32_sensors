#!/usr/bin/env python3
"""Interactive, host-only check of CLion's ESP-IDF tooltip workflow.

The wizard starts the dev container and CLion, then prints one manual step
at a time. Every step needs an explicit `y` or `n`, so a silent or unattended
run cannot pass. CI must not run this module: the `dev.py` command that uses
it refuses when `CI` is set.

Input and process starting are injected so the whole procedure is covered by
`scripts/test_dev.py` without launching a real IDE. The normal entry point is
`scripts/dev.py clion-manual-test`; direct execution is also supported.
"""

from __future__ import annotations

from pathlib import Path
import os
import shutil
from typing import Callable, Mapping, NamedTuple

CLION_LAUNCHER_ENV = "CLION_LAUNCHER"

# Paths are relative to the host user's home directory. Toolbox puts a
# `clion` wrapper on PATH, but the wrapper's directory is not always present
# in a non-login shell, so the known locations are checked as a fallback.
TOOLBOX_LAUNCHERS = (
    ".local/share/JetBrains/Toolbox/scripts/clion",
    ".local/share/JetBrains/Toolbox/scripts/clion1",
    ".local/share/JetBrains/Toolbox/apps/clion/bin/clion",
)

YES_ANSWERS = frozenset(("y", "yes"))
NO_ANSWERS = frozenset(("n", "no"))


class Step(NamedTuple):
    """One manual action and the question that records its result."""

    title: str
    instructions: tuple[str, ...]
    prompt: str


STEPS = (
    Step(
        title="Connect CLion to the compilation database",
        instructions=(
            "Wait for the CLion window to appear.",
            'Select "Remote Connections", then "ssh".',
            'Use "ssh -p 2222 dev@localhost" with project directory '
            '"/project/.clion/compile_commands.json".',
            "Select Open Project",
        ),
        prompt="Did CLion open the compile_commands.json project?",
    ),
    Step(
        title="Confirm the compilation-database project",
        instructions=(
            "The project tree root should be .clion, not esp32_sensors.",
            "If the repo root opened instead, use File > Open on "
            "/project/.clion/compile_commands.json.",
            'Choose "Open as Project", not "Open as File".',
            "Wait for indexing to finish.",
        ),
        prompt="Is the .clion compilation-database project open?",
    ),
    Step(
        title="Check compilation-database path settings",
        instructions=(
            "Sources in the database use container-absolute /project paths.",
            "Use Tools > Compilation Database > Change Project Root only if "
            "those paths do not resolve.",
            "The verified setup needed no change; set /project when asked.",
        ),
        prompt="Are /project source paths resolved?",
    ),
    Step(
        title="Check the Compilation Database toolchain",
        instructions=(
            "Open Settings > Build, Execution, Deployment > "
            "Compilation Database.",
            'Leave Toolchain as "Use Default".',
            "In the container Default is the local System toolchain.",
        ),
        prompt="Is the toolchain set to Use Default?",
    ),
    Step(
        title="Verify the ESP-IDF tooltip",
        instructions=(
            "In the .clion project tree double-click "
            "components/espidfcpp/libespidfcpp/include/espidf_cpp/timer.hpp.",
            "Wait for indexing to settle, then hover esp_timer_start_once.",
            'The tooltip should show "Start a one-shot timer" with the '
            "ESP-IDF @brief text.",
        ),
        prompt="Did the ESP-IDF Doxygen tooltip appear?",
    ),
)


def find_clion_launcher(
    env: Mapping[str, str] | None = None,
    which: Callable[[str], str | None] = shutil.which,
    home: Path | None = None,
) -> str | None:
    """Return the host command that starts CLion, or None if not found.

    `CLION_LAUNCHER` wins so a different Toolbox installation can be used
    without editing the repository. Next comes `clion` on PATH, then the
    known Toolbox locations for an interactive launch from the desktop.
    """
    environment = os.environ if env is None else env
    override = environment.get(CLION_LAUNCHER_ENV, "").strip()
    if override:
        return override

    on_path = which("clion")
    if on_path:
        return on_path

    home_directory = Path.home() if home is None else home
    for relative_path in TOOLBOX_LAUNCHERS:
        candidate = home_directory / relative_path
        if candidate.is_file():
            return str(candidate)
    return None


def prompt_yes_no(
    prompt: str,
    *,
    input_fn: Callable[[], str] = input,
    output: Callable[[str], None] = print,
) -> bool | None:
    """Ask until the user answers yes or no; None means an aborted run.

    Any other answer is rejected, so an accidental Enter cannot record a
    pass. EOF and Ctrl-C also abort instead of looking like a failure.
    """
    while True:
        output(f"{prompt} [y/n]: ")
        try:
            answer = input_fn().strip().lower()
        except (EOFError, KeyboardInterrupt):
            output("\nAborted; no answer recorded.")
            return None

        if answer in YES_ANSWERS:
            return True
        if answer in NO_ANSWERS:
            return False
        output("Please answer 'y' or 'n'.")


def run_manual_test(
    *,
    launcher: str | None,
    start_container: Callable[[], int],
    launch_clion: Callable[[str], int],
    prompt: Callable[[str], bool | None],
    say: Callable[[str], None] = print,
    database_exists: Callable[[], bool] = lambda: True,
    remove_containers: Callable[[], int] = lambda: 0,
    archive_path: str | None = None,
    seed_archive: Callable[[], int] = lambda: 0,
    check_backend_log: Callable[[], tuple[bool, str]] = (
        lambda: (True, "backend log check not configured")
    ),
) -> int:
    """Run the guided procedure and return a process exit code.

    Every side effect that a test needs to observe is injected. A failed
    precondition or manual step returns 1; only all five confirmed steps
    return 0. With `archive_path`, the backend download must be absent
    from the Toolbox log after the user confirms the first step.
    """
    if not launcher:
        say("clion-manual-test: no CLion launcher found.")
        say(f"Set {CLION_LAUNCHER_ENV} to the CLion command and retry.")
        return 1

    if not database_exists():
        say("clion-manual-test: .clion/compile_commands.json is missing.")
        say("Run `scripts/dev.py build` and `clion-compdb` first.")
        return 1

    say("Removing any existing dev containers...")
    if remove_containers() != 0:
        say("FAILED: the dev containers did not stop cleanly.")
        return 1

    say("Starting a fresh dev container...")
    if start_container() != 0:
        say("FAILED: the dev container did not start.")
        return 1

    if archive_path:
        say(f"Uploading CLion backend archive: {archive_path}")
        if seed_archive() != 0:
            say("FAILED: the CLion backend archive was not uploaded.")
            return 1
    else:
        say("No CLion backend archive found in ~/Downloads.")

    say(f"Starting CLion: {launcher}")
    if launch_clion(launcher) != 0:
        say("FAILED: CLion did not start.")
        return 1

    for number, step in enumerate(STEPS, start=1):
        say("")
        say(f"Step {number}/{len(STEPS)}: {step.title}")
        for line in step.instructions:
            say(f"  - {line}")
        if number == 1 and archive_path:
            say("  - The archive was uploaded; downloading should be skipped.")
        elif number == 1:
            say('  - Wait for "Downloading IDE backend..." to finish.')

        answer = prompt(step.prompt)
        if answer is None:
            say("Aborted by the user.")
            return 1
        if not answer:
            say(f"FAILED: step {number}: {step.title}")
            return 1
        say("  OK")

        if number == 1 and archive_path:
            skipped, detail = check_backend_log()
            say(f"  - {detail}")
            if not skipped:
                say("FAILED: the IDE backend download was not skipped.")
                return 1

    say("")
    say("PASS: the CLion ESP-IDF tooltip workflow was verified manually.")
    return 0


def main() -> int:
    """Run the wizard through the single `dev.py` implementation.

    The import is local because `dev.py` imports this module at startup;
    direct execution needs the reverse direction once, after Python has
    put `scripts/` on `sys.path`.
    """
    import dev

    return dev.cmd_clion_manual_test()


if __name__ == "__main__":
    raise SystemExit(main())
