#!/usr/bin/env python3
"""Host-side unit tests for `scripts/dev.py`.

Run with:

    scripts/dev.py self-check

These exercise dev.py's own pure helpers (no Docker hop, no ESP-IDF): the
`shell` command line it builds, and `shell`'s `-C DIR` path resolution. They
use the standard-library `unittest` runner so no third-party dependency is
needed. `dev.py` is imported in-process, so the tests run against the code
being checked rather than a copy.
"""

from __future__ import annotations

from contextlib import redirect_stderr, redirect_stdout
import io
import os
from pathlib import Path
import subprocess
import sys
import unittest

# Make `import dev` work regardless of the caller's cwd: add this script's
# directory (scripts/) to sys.path, then import the module under test.
SCRIPTS_DIR = Path(__file__).resolve().parent
if str(SCRIPTS_DIR) not in sys.path:
    sys.path.insert(0, str(SCRIPTS_DIR))

import dev  # noqa: E402  (import after sys.path is prepared)


class BuildShellCommandTests(unittest.TestCase):
    """`_build_shell_command` — the argv `cmd_shell` hands to `run()`."""

    def test_no_args_starts_interactive_login_shell(self):
        # No command and no -C: a bare interactive login shell.
        self.assertEqual(dev._build_shell_command([]), ["bash", "-l"])

    def test_single_argument_runs_through_login_shell(self):
        self.assertEqual(
            dev._build_shell_command(["ls"]),
            ["bash", "-l", "-c", "ls"],
        )

    def test_multiple_arguments_are_joined_verbatim(self):
        # Separate argv words (e.g. `shell ls -1d /`) become one command line.
        self.assertEqual(
            dev._build_shell_command(["ls", "-1d", "/"]),
            ["bash", "-l", "-c", "ls -1d /"],
        )

    def test_shell_syntax_in_one_argument_is_not_requoted(self):
        # Regression: a single argument containing shell syntax must reach bash
        # as-is (pipes/redirects/semicolons intact), NOT as one quoted token.
        self.assertEqual(
            dev._build_shell_command(["echo hi | wc -l"]),
            ["bash", "-l", "-c", "echo hi | wc -l"],
        )

    def test_semicolons_and_redirects_survive(self):
        cmd = dev._build_shell_command(
            ["ls /x 2>/dev/null; echo ---; grep -rl y /x 2>/dev/null"]
        )
        self.assertEqual(
            cmd,
            [
                "bash",
                "-l",
                "-c",
                "ls /x 2>/dev/null; echo ---; grep -rl y /x 2>/dev/null",
            ],
        )

    def test_leading_dash_argument_is_passed_to_the_command(self):
        # `-1d` is an argument to `ls`, not a bash option; it must land in the
        # -c string verbatim.
        self.assertEqual(
            dev._build_shell_command(["ls", "-1d", "/"]),
            ["bash", "-l", "-c", "ls -1d /"],
        )

    def test_directory_prefixes_a_cd(self):
        self.assertEqual(
            dev._build_shell_command(["pwd"], directory="components"),
            ["bash", "-l", "-c", "cd /project/components && pwd"],
        )

    def test_absolute_directory_is_used_verbatim(self):
        self.assertEqual(
            dev._build_shell_command(["pwd"], directory="/project/test"),
            ["bash", "-l", "-c", "cd /project/test && pwd"],
        )

    def test_directory_is_shell_quoted(self):
        # A directory with spaces must be quoted so the `cd` sees one argument.
        cmd = dev._build_shell_command(["pwd"], directory="a b/c")
        self.assertEqual(cmd[:3], ["bash", "-l", "-c"])
        self.assertEqual(cmd[3], "cd '/project/a b/c' && pwd")

    def test_directory_without_command_execs_interactive_login_shell(self):
        self.assertEqual(
            dev._build_shell_command([], directory="components"),
            ["bash", "-l", "-c", "cd /project/components && exec bash -l"],
        )


class ResolveContainerDirectoryTests(unittest.TestCase):
    """`_resolve_container_directory` — `shell -C DIR` path handling."""

    def test_relative_is_resolved_under_project_root(self):
        self.assertEqual(
            dev._resolve_container_directory("components"), "/project/components"
        )

    def test_nested_relative_is_resolved_under_project_root(self):
        self.assertEqual(
            dev._resolve_container_directory("test/build_linux"),
            "/project/test/build_linux",
        )

    def test_absolute_is_used_verbatim(self):
        self.assertEqual(
            dev._resolve_container_directory("/project/test"),
            "/project/test",
        )

    def test_absolute_below_root_is_used_verbatim(self):
        self.assertEqual(dev._resolve_container_directory("/tmp"), "/tmp")


class SubprojectBuildTests(unittest.TestCase):
    """Subproject layout: `_idf()`, `build` argument parsing, root build."""

    def test_subprojects_are_firmware_and_host_tests(self):
        self.assertEqual(
            dev.SUBPROJECTS,
            ("mcgowan", "test", "espidf-cpp-test"),
        )

    def test_idf_runs_in_subproject_with_build_tree_inside_it(self):
        self.assertEqual(
            dev._idf("mcgowan", "build_esp32s3", "build"),
            [
                "idf.py",
                "-C", "mcgowan",
                "-B", "mcgowan/build_esp32s3",
                "build",
            ],
        )

    def test_idf_passes_flash_monitor_through_in_order(self):
        self.assertEqual(
            dev._idf("mcgowan", "build_esp32s3", "flash", "monitor")[5:],
            ["flash", "monitor"],
        )

    def test_no_arguments_selects_every_subproject(self):
        self.assertEqual(dev._parse_subprojects([]), list(dev.SUBPROJECTS))

    def test_named_subprojects_are_kept_in_argument_order(self):
        self.assertEqual(
            dev._parse_subprojects(["test", "mcgowan"]),
            ["test", "mcgowan"],
        )

    def test_unknown_subproject_is_rejected(self):
        # Fail loudly instead of silently building nothing.
        err = io.StringIO()
        with redirect_stderr(err):
            self.assertIsNone(dev._parse_subprojects(["mcgowan", "bogus"]))
        self.assertIn("bogus", err.getvalue())

    def test_root_build_configures_then_builds_all_subprojects(self):
        self.assertEqual(
            dev._root_build_commands(list(dev.SUBPROJECTS)),
            [
                [
                    "cmake",
                    "-S", ".",
                    "-B", dev.ROOT_BUILD_DIR,
                    "-DSUBPROJECTS=mcgowan;test;espidf-cpp-test",
                ],
                ["cmake", "--build", dev.ROOT_BUILD_DIR],
            ],
        )

    def test_firmware_binary_is_named_after_the_project(self):
        # The ESP-IDF project name (mcgowan/) determines the app binary name.
        self.assertEqual(dev.FW_BINARY, "mcgowan.bin")
        self.assertEqual(dev.FIRMWARE_PROJECT, "mcgowan")

    def test_subproject_directories_have_a_cmakelists(self):
        for project in dev.SUBPROJECTS:
            with self.subTest(project=project):
                cmake = dev.REPO_ROOT / project / "CMakeLists.txt"
                self.assertTrue(cmake.is_file(), f"missing {cmake}")

    def test_root_is_not_an_espidf_project(self):
        # The root aggregates subprojects with plain CMake; it must not
        # include ESP-IDF's project.cmake (which would make it a project).
        root_cmake = (dev.REPO_ROOT / "CMakeLists.txt").read_text()
        self.assertNotIn("tools/cmake/project.cmake", root_cmake)
        # Every aggregate target is prefixed with its subproject name.
        for project in dev.SUBPROJECTS:
            self.assertIn(
                f"_idf_subproject({project} ", root_cmake,
                f"no {project}_build target in the root CMakeLists.txt",
            )


class CommandRegistryTests(unittest.TestCase):
    """The `COMMANDS` table and the argparse wiring stay in sync."""

    def test_self_check_is_registered(self):
        self.assertIn("self-check", dev.COMMANDS)
        self.assertIs(dev.COMMANDS["self-check"], dev.cmd_self_check)

    def test_build_espidf_cpp_test_is_registered(self):
        self.assertIn("build-espidf-cpp-test", dev.COMMANDS)
        self.assertIs(
            dev.COMMANDS["build-espidf-cpp-test"],
            dev.cmd_build_espidf_cpp_test,
        )

    def test_host_test_projects_cover_both_suites(self):
        projects = [project for project, _ in dev.HOST_TEST_PROJECTS]
        self.assertEqual(
            projects,
            [dev.TEST_PROJECT, dev.ESPIDF_CPP_TEST_PROJECT],
        )
        # Every host-test project is also a buildable subproject.
        for project in projects:
            self.assertIn(project, dev.SUBPROJECTS)

    def test_complete_skips_itself(self):
        # The internal `complete` helper must not advertise itself.
        old = sys.argv
        try:
            sys.argv = ["dev.py", "complete", ""]
            buf = io.StringIO()
            with redirect_stdout(buf):
                dev.cmd_complete()
        finally:
            sys.argv = old
        names = buf.getvalue().split()
        self.assertNotIn("complete", names)
        self.assertIn("self-check", names)

    def test_every_registered_command_has_a_docstring(self):
        # argparse uses the first docstring line as the command's help text.
        for name, handler in dev.COMMANDS.items():
            if name == "complete":
                continue
            self.assertTrue(
                (handler.__doc__ or "").strip(),
                f"command {name!r} has no docstring",
            )


class BuildProjectTests(unittest.TestCase):
    """`_build_project` picks the target-specific build directory."""

    def setUp(self):
        # Record the commands `run()` would execute instead of executing them.
        self.calls = []
        self._original_run = dev.run

        def fake_run(cmd, cwd=dev.REPO_ROOT):
            self.calls.append(list(cmd))
            return 0

        dev.run = fake_run

    def tearDown(self):
        dev.run = self._original_run

    def test_espidf_cpp_test_is_built_for_linux(self):
        self.assertEqual(dev._build_project(dev.ESPIDF_CPP_TEST_PROJECT), 0)
        self.assertEqual(
            self.calls,
            [
                dev._idf(
                    dev.ESPIDF_CPP_TEST_PROJECT,
                    dev.TEST_BUILD_DIR,
                    "--preview",
                    "set-target",
                    "linux",
                ),
                dev._idf(
                    dev.ESPIDF_CPP_TEST_PROJECT,
                    dev.TEST_BUILD_DIR,
                    "build",
                ),
            ],
        )

    def test_firmware_is_built_for_the_chip(self):
        self.assertEqual(dev._build_project(dev.FIRMWARE_PROJECT), 0)
        self.assertEqual(
            self.calls,
            [dev._idf(dev.FIRMWARE_PROJECT, dev.FW_BUILD_DIR, "build")],
        )


class GitHooksInstallationTests(unittest.TestCase):
    """`install-hooks` invariants: the tracked hook is present and active.

    These guard the bug where `install-hooks` reported success (because it only
    set `core.hooksPath`) while the hook file itself was missing, so commit-time
    checks were silently skipped.
    """

    GITHOOKS_DIR = Path(".githooks")
    HOOK_NAME = "pre-commit"

    def _git(self, *args):
        """Run git in the repo root and return stdout (text)."""
        proc = subprocess.run(
            ("git", *args),
            cwd=dev.REPO_ROOT,
            capture_output=True,
            text=True,
            check=True,
        )
        return proc.stdout

    def _get_hooks_path(self):
        """Return `core.hooksPath`, or None when it is unset.

        `git config --get` exits non-zero when the key is unset (e.g. on a
        fresh CI checkout that never ran `install-hooks`), so we tolerate that
        rather than letting `check=True` raise.
        """
        proc = subprocess.run(
            ("git", "config", "--get", "core.hooksPath"),
            cwd=dev.REPO_ROOT,
            capture_output=True,
            text=True,
        )
        if proc.returncode != 0:
            return None
        return proc.stdout.strip()

    def _set_hooks_path(self, value):
        """Set or unset `core.hooksPath` (value=None unsets it)."""
        if value is None:
            subprocess.run(
                ("git", "config", "--unset", "core.hooksPath"),
                cwd=dev.REPO_ROOT,
                capture_output=True,
                text=True,
            )
        else:
            subprocess.run(
                ("git", "config", "core.hooksPath", value),
                cwd=dev.REPO_ROOT,
                capture_output=True,
                text=True,
                check=True,
            )

    def test_hook_file_exists_and_is_executable(self):
        # The working tree must contain the runnable hook, not just the config
        # pointing at an empty directory.
        hook = dev.REPO_ROOT / self.GITHOOKS_DIR / self.HOOK_NAME
        self.assertTrue(hook.is_file(), f"missing hook file: {hook}")
        self.assertTrue(
            os.access(hook, os.X_OK),
            f"hook file is not executable: {hook}",
        )

    def test_hook_file_is_tracked_by_git(self):
        # The hook is version-controlled, so a fresh clone can restore it.
        tracked = self._git("ls-files", "--", str(self.GITHOOKS_DIR))
        self.assertIn(
            f"{self.GITHOOKS_DIR}/{self.HOOK_NAME}",
            tracked.splitlines(),
        )

    def test_install_hooks_sets_core_hooks_path(self):
        # `install-hooks` must point git at the version-controlled hooks dir.
        # Run the real command and restore the prior config afterwards so the
        # test does not depend on (or permanently change) the local clone.
        original = self._get_hooks_path()
        try:
            rc = dev.cmd_install_hooks()
            self.assertEqual(rc, 0)
            self.assertEqual(self._get_hooks_path(), str(self.GITHOOKS_DIR))
        finally:
            self._set_hooks_path(original)

    def test_installed_hook_matches_the_tracked_content(self):
        # The file on disk must be the tracked content (the "latest changes"):
        # a deleted-then-restored hook must be byte-identical to the version
        # git tracks, which is what `install-hooks` restores. Compare against
        # the index (staged) version, falling back to HEAD when the hook is
        # not staged — mirroring the restore logic.
        rel = f"{self.GITHOOKS_DIR}/{self.HOOK_NAME}"
        tracked = None
        for source in (f":{rel}", f"HEAD:{rel}"):
            proc = subprocess.run(
                ("git", "show", source),
                cwd=dev.REPO_ROOT,
                capture_output=True,
                text=True,
            )
            if proc.returncode == 0:
                tracked = proc.stdout
                break
        self.assertIsNotNone(tracked, f"{rel} is not tracked by git")
        on_disk = (dev.REPO_ROOT / self.GITHOOKS_DIR / self.HOOK_NAME).read_text()
        self.assertEqual(on_disk, tracked)


class SelfCheckModuleTests(unittest.TestCase):
    """Sanity check on the module dev.py invokes for `self-check`."""

    def test_self_check_module_points_at_this_file(self):
        # `SELF_CHECK_MODULE` is dotted (`scripts.test_dev`); its final
        # component must name this test module so `python3 -m unittest` runs
        # these tests.
        self.assertEqual(dev.SELF_CHECK_MODULE, "scripts.test_dev")
        self.assertEqual(dev.SELF_CHECK_MODULE.split(".")[-1], "test_dev")

    def test_self_check_module_file_exists(self):
        # From the repo root, `scripts/test_dev.py` must exist.
        expected = dev.REPO_ROOT / "scripts" / "test_dev.py"
        self.assertTrue(os.path.isfile(expected))


if __name__ == "__main__":
    unittest.main()
