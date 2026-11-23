#!/usr/bin/env python3
"""Interactive, host-only check of CLion's ESP-IDF tooltip workflow.

The wizard first refuses when CLion is already running on the host, then
kills and deletes the dev container. It then runs `up`, `build`,
`clion-compdb`, and `clion-run-configs`, starts CLion, and prints one manual
step at a time. Before the first prompt the Toolbox log is polled for the
opened remote project, with a 10-minute wait that the user may extend; a wrong
project fails at once. The tooltip check follows the remote-project connection;
the compilation-database checks are sub-steps 2.1-2.3 and run only when the
tooltip check fails. Every step needs an explicit `y` or `n`, so a silent or
unattended run cannot pass. CI must not run this module: the `dev.py` command
that uses it refuses when `CI` is set.

Input and process starting are injected so the whole procedure is covered by
`scripts/test_dev.py` without launching a real IDE. The normal entry point is
`scripts/dev.py clion-manual-test`; direct execution is also supported.
"""

from __future__ import annotations

from pathlib import Path
import os
import shutil
import time
from typing import Callable, Mapping, NamedTuple

CLION_LAUNCHER_ENV = "CLION_LAUNCHER"

# Verdicts from scanning the Toolbox log while CLion opens the remote project.
# A missing directory means CLion has not connected yet; a different one is a
# failure that must not wait for a timeout.
PROJECT_OPEN_PENDING = "pending"
PROJECT_OPEN_READY = "ready"
PROJECT_OPEN_WRONG = "wrong"

PROJECT_WAIT_SECONDS = 600.0
PROJECT_POLL_SECONDS = 10.0

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
    """One manual action and the question that records its result.

    `label` is the number shown in the wizard. The 2.x steps are the
    compilation-database fallback: they are shown only when the tooltip check
    (step 2) fails.
    """

    label: str
    title: str
    instructions: tuple[str, ...]
    prompt: str
    on_tooltip_failure: bool = False


TOOLTIP_TITLE = "Verify the ESP-IDF tooltip"
RUN_CONFIG_TITLE = "Run the espidfcpp linux test configuration"

STEPS = (
    Step(
        label="1",
        title="Connect CLion to the remote project",
        instructions=(
            "Wait for the CLion window to appear.",
            'Select "Remote Connections", then "ssh".',
            'Use "ssh -p 2222 dev@localhost" with project directory '
            '"/project".',
            "Select Open Project.",
        ),
        prompt="Did CLion open the /project remote project?",
    ),
    Step(
        label="2",
        title=TOOLTIP_TITLE,
        instructions=(
            "In the .clion project tree double-click "
            "components/espidfcpp/libespidfcpp/include/espidf_cpp/timer.hpp.",
            "Wait for indexing to settle, then hover esp_timer_start_once.",
            'The tooltip should show "Start a one-shot timer" with the '
            "ESP-IDF @brief text.",
        ),
        prompt="Did the ESP-IDF Doxygen tooltip appear?",
    ),
    Step(
        label="2.1",
        title="Open the compilation database for tooltips",
        instructions=(
            "The repository root project should be open at /project.",
            "Open the compilation database so ESP-IDF symbols resolve: "
            "use File > Open on /project/.clion/compile_commands.json "
            "and choose \"Open as Project\" if needed.",
            "Wait for indexing to finish.",
        ),
        prompt="Is the compilation database available for tooltips?",
        on_tooltip_failure=True,
    ),
    Step(
        label="2.2",
        title="Check compilation-database path settings",
        instructions=(
            "Sources in the database use container-absolute /project paths.",
            "Use Tools > Compilation Database > Change Project Root only if "
            "those paths do not resolve.",
            "The verified setup needed no change; set /project when asked.",
        ),
        prompt="Are /project source paths resolved?",
        on_tooltip_failure=True,
    ),
    Step(
        label="2.3",
        title="Check the Compilation Database toolchain",
        instructions=(
            "Open Settings > Build, Execution, Deployment > "
            "Compilation Database.",
            'Leave Toolchain as "Use Default".',
            "In the container Default is the local System toolchain.",
        ),
        prompt="Is the toolchain set to Use Default?",
        on_tooltip_failure=True,
    ),
    Step(
        label="3",
        title=RUN_CONFIG_TITLE,
        instructions=(
            "Open the repository project `/project` in CLion if needed "
            "(the `.clion` compilation-database project does not show the "
            "shared run configurations from `.idea/runConfigurations`).",
            "Run / Run ...",
            "In Run/Debug configurations select choose",
            "test-espidf-cpp / Run",
            "Verify the project executed in the dev container",
        ),
        prompt="Did test-espidf-cpp / Run execute in the dev container?",
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


def wait_for_remote_project(
    check_opened_project: Callable[[], tuple[str, str]],
    prompt: Callable[[str], bool | None],
    say: Callable[[str], None],
    *,
    timeout_seconds: float = PROJECT_WAIT_SECONDS,
    poll_seconds: float = PROJECT_POLL_SECONDS,
    sleep: Callable[[float], None] = time.sleep,
    clock: Callable[[], float] = time.monotonic,
) -> bool:
    """Wait for CLion to open `/project`; return False to abort the wizard.

    The Toolbox log is polled instead of assuming the user can answer the
    connection prompt immediately. A wrong directory fails at once; no
    directory after `timeout_seconds` asks whether to wait another full
    interval. Declining or aborting that question fails the run.
    """
    say("  - Scanning the Toolbox log for the opened remote project...")
    minutes = int(timeout_seconds // 60)
    while True:
        deadline = clock() + timeout_seconds
        while clock() < deadline:
            status, detail = check_opened_project()
            if status != PROJECT_OPEN_PENDING:
                say(f"  - {detail}")
                if status == PROJECT_OPEN_WRONG:
                    say("FAILED: the wrong project is opened in CLion.")
                    return False
                return True
            sleep(poll_seconds)

        answer = prompt(
            f"No project opened in {minutes} minutes; wait some more?"
        )
        if answer is None:
            say("Aborted by the user.")
            return False
        if not answer:
            say(f"FAILED: no project was opened in {minutes} minutes.")
            return False


def run_manual_test(
    *,
    launcher: str | None,
    check_clion_running: Callable[[], bool],
    stop_container: Callable[[], int],
    start_container: Callable[[], int],
    build: Callable[[], int],
    clion_compdb: Callable[[], int],
    clion_run_configs: Callable[[], int],
    launch_clion: Callable[[str], int],
    prompt: Callable[[str], bool | None],
    say: Callable[[str], None] = print,
    archive_path: str | None = None,
    seed_archive: Callable[[], int] = lambda: 0,
    check_backend_log: Callable[[], tuple[bool, str]] = (
        lambda: (True, "backend log check not configured")
    ),
    check_remote_project_dir: Callable[[], tuple[bool, str]] = (
        lambda: (True, "remote project directory check not configured")
    ),
    check_opened_project: Callable[[], tuple[str, str]] = (
        lambda: (PROJECT_OPEN_READY, "project check not configured")
    ),
    timeout_seconds: float = PROJECT_WAIT_SECONDS,
    poll_seconds: float = PROJECT_POLL_SECONDS,
    sleep: Callable[[float], None] = time.sleep,
    clock: Callable[[], float] = time.monotonic,
) -> int:
    """Run the guided procedure and return a process exit code.

    Every side effect that a test needs to observe is injected. The first two
    things done are `check_clion_running` (an already-open local CLion fails
    and aborts before anything else) and `stop_container` (kill and delete the
    dev container). A failed precondition, setup command, log check, or manual
    step returns 1; only all confirmed steps return 0. Setup then runs `up`,
    `build`, `clion-compdb`, and `clion-run-configs` in order before CLion is
    started. With `archive_path`, the backend download must be absent from the
    Toolbox log after the user confirms the first step. The remote project
    directory is checked from the Toolbox log before the tooltip and run
    prompts. Before the first prompt, `check_opened_project` is polled until
    `/project` is opened; after `timeout_seconds` the user is asked whether to
    wait longer, and a wrong directory fails immediately.

    Step 2 (the tooltip check) follows step 1 directly. The 2.x steps are its
    compilation-database fallback and are skipped when it succeeds; when it
    fails they run in order and the wizard still fails on the tooltip step.
    """
    if check_clion_running():
        say("FAILED: CLion is already running locally; close it and retry.")
        return 1

    say("Stopping and removing the dev container...")
    if stop_container() != 0:
        say("FAILED: the dev container could not be stopped and removed.")
        return 1

    if not launcher:
        say("clion-manual-test: no CLion launcher found.")
        say(f"Set {CLION_LAUNCHER_ENV} to the CLion command and retry.")
        return 1

    setup_steps = (
        ("Starting the dev container...", start_container,
         "FAILED: the dev container did not start."),
        ("Building the projects...", build,
         "FAILED: the build did not complete."),
        ("Generating the compilation database...", clion_compdb,
         "FAILED: the compilation database was not generated."),
        ("Generating CLion run/debug configurations...", clion_run_configs,
         "FAILED: the run/debug configurations were not generated."),
    )
    for message, action, failure in setup_steps:
        say(message)
        if action() != 0:
            say(failure)
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

    tooltip_failed = False
    for step in STEPS:
        if step.on_tooltip_failure and not tooltip_failed:
            continue

        if step.title in (TOOLTIP_TITLE, RUN_CONFIG_TITLE):
            ok, detail = check_remote_project_dir()
            say(f"  - {detail}")
            if not ok:
                say("FAILED: the remote project directory is incorrect.")
                return 1

        say("")
        say(f"Step {step.label}/{len(STEPS)}: {step.title}")
        for line in step.instructions:
            say(f"  - {line}")
        if step.label == "1" and archive_path:
            say("  - The archive was uploaded; downloading should be skipped.")
        elif step.label == "1":
            say('  - Wait for "Downloading IDE backend..." to finish.')
        if step.label == "1":
            if not wait_for_remote_project(
                check_opened_project,
                prompt,
                say,
                timeout_seconds=timeout_seconds,
                poll_seconds=poll_seconds,
                sleep=sleep,
                clock=clock,
            ):
                return 1

        answer = prompt(step.prompt)
        if answer is None:
            say("Aborted by the user.")
            return 1
        if not answer:
            if step.title == TOOLTIP_TITLE and not tooltip_failed:
                tooltip_failed = True
                say("  Tooltip failed; running the compilation-database "
                    "checks.")
                continue
            say(f"FAILED: step {step.label}: {step.title}")
            return 1
        say("  OK")

        if step.label == "1" and archive_path:
            skipped, detail = check_backend_log()
            say(f"  - {detail}")
            if not skipped:
                say("FAILED: the IDE backend download was not skipped.")
                return 1

        if step.label == "2.3":
            say(f"FAILED: step 2: {TOOLTIP_TITLE}")
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

# CODE INSPECTION FINDING (scripts/clion_manual_test.py:226-233)
# wait_for_remote_project() never shows pending details from
# check_opened_project(). If the Toolbox log cannot be read, the wizard polls
# silently until timeout. See also scripts/dev.py:2790-2804.
