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

from contextlib import contextmanager, redirect_stderr, redirect_stdout
import io
import os
from pathlib import Path
import stat
import subprocess
import sys
import tempfile
import unittest
from unittest import mock

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

    def test_subprojects_are_firmware_host_tests_and_donny(self):
        self.assertEqual(
            dev.SUBPROJECTS,
            ("mcgowan", "test", "espidf-cpp-test", "donny"),
        )

    def test_linux_projects_are_the_host_target_subprojects(self):
        self.assertEqual(
            dev.LINUX_PROJECTS,
            (dev.TEST_PROJECT, dev.ESPIDF_CPP_TEST_PROJECT, dev.DONNY_PROJECT),
        )
        # Every linux project is also a buildable subproject.
        for project in dev.LINUX_PROJECTS:
            self.assertIn(project, dev.SUBPROJECTS)

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
                    "-DSUBPROJECTS=mcgowan;test;espidf-cpp-test;donny",
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

    def test_donny_is_built_for_linux(self):
        self.assertEqual(dev._build_project(dev.DONNY_PROJECT), 0)
        self.assertEqual(
            self.calls,
            [
                dev._idf(
                    dev.DONNY_PROJECT,
                    dev.TEST_BUILD_DIR,
                    "--preview",
                    "set-target",
                    "linux",
                ),
                dev._idf(dev.DONNY_PROJECT, dev.TEST_BUILD_DIR, "build"),
            ],
        )

    def test_donny_is_built_but_not_run_as_a_host_test(self):
        # `donny` is an application (app_main spins forever), so `test-linux`
        # must build it but never run it.
        self.assertNotIn(dev.DONNY_PROJECT, dict(dev.HOST_TEST_PROJECTS))

    def test_firmware_is_built_for_the_chip(self):
        self.assertEqual(dev._build_project(dev.FIRMWARE_PROJECT), 0)
        self.assertEqual(
            self.calls,
            [dev._idf(dev.FIRMWARE_PROJECT, dev.FW_BUILD_DIR, "build")],
        )


class DevImageTests(unittest.TestCase):
    """The dev image is pulled (prebuilt), not rebuilt on every machine.

    `docker-compose.yml` pins the prebuilt image as the `dev` service's
    `image:`; the `build:` section stays so the image can still be rebuilt from
    the committed Dockerfile. `dev.py` exposes only `pull` — there is no
    `image` (build) command in the normal workflow.
    """

    def setUp(self):
        self.compose = (dev.REPO_ROOT / "docker-compose.yml").read_text()

    def test_pinned_image_is_the_prebuilt_registry_image(self):
        self.assertEqual(dev.DEV_IMAGE, "marcpawl/esp-idf:20261025")

    def test_compose_service_uses_the_pinned_image(self):
        self.assertIn(f"image: {dev.DEV_IMAGE}", self.compose)

    def test_compose_still_keeps_the_dockerfile_build(self):
        # Rebuilding must remain possible from the committed Dockerfile.
        self.assertTrue((dev.REPO_ROOT / "Dockerfile").is_file())
        self.assertIn("build:", self.compose)
        self.assertIn("dockerfile: Dockerfile", self.compose)

    def test_local_rebuild_tag_differs_from_the_pinned_image(self):
        # A developer's own rebuild must never masquerade as the shared image.
        self.assertNotEqual(dev.LOCAL_IMAGE, dev.DEV_IMAGE)

    def test_pull_is_registered_and_build_is_not(self):
        self.assertIs(dev.COMMANDS.get("pull"), dev.cmd_pull)
        self.assertNotIn("image", dev.COMMANDS)

    def test_pull_pulls_the_pinned_image(self):
        calls = []
        original_run, original_in_container = dev.run, dev.is_in_container
        dev.run = lambda cmd, cwd=dev.REPO_ROOT: calls.append(list(cmd)) or 0
        dev.is_in_container = lambda: False
        try:
            self.assertEqual(dev.cmd_pull(), 0)
        finally:
            dev.run, dev.is_in_container = original_run, original_in_container
        self.assertEqual(calls, [["docker", "pull", dev.DEV_IMAGE]])

    def test_pull_is_refused_inside_the_container(self):
        original_in_container = dev.is_in_container
        dev.is_in_container = lambda: True
        err = io.StringIO()
        try:
            with redirect_stderr(err):
                self.assertEqual(dev.cmd_pull(), 1)
        finally:
            dev.is_in_container = original_in_container
        self.assertIn("container", err.getvalue())


class ContainerRunTests(unittest.TestCase):
    """`compose_run` starts the container as a user that can write its $HOME.

    Regression for the CI failure: GitHub runners use uid 1001, `dev.py`
    passed that through as `--user 1001:1001`, and the image's $HOME
    (`/home/dev`, owned by 1000) was then unwritable, so ccache died with
    "ccache: error: Permission denied" and git rejected the root-owned IDF
    tree as "dubious ownership".
    """

    def setUp(self):
        self.calls = []
        self.reply = [0]  # exit status `run` reports (0 == success)
        self._original_run = dev.run

        def fake_run(cmd, cwd=dev.REPO_ROOT, env=None):
            self.calls.append((list(cmd), dict(env or {})))
            return self.reply[0]

        dev.run = fake_run

    def tearDown(self):
        dev.run = self._original_run

    @contextmanager
    def _checkout(self, owner_uid: int, uid: int):
        """A temp project mount owned by `owner_uid`, driven by host `uid`.

        Patches the identity/ownership seams `_reclaim_checkout` reads, so the
        CI case (a checkout the host user does not own, or a mount whose owner
        is not the container user) is reproducible without Docker. `owner_uid`
        is what the *mount* reports (the ownership the container is subject
        to); the host's own view is deliberately different.
        """
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            tracked = root / "mcgowan"
            tracked.mkdir()
            (tracked / "fake.cpp").write_text("")
            mount = root / "project"
            mount.mkdir()
            # Report every path as a directory so the tracked entries
            # qualify as probe targets (`Path.is_dir` consults `stat`).
            owner = type(
                "Owner",
                (),
                {"st_uid": owner_uid, "st_mode": stat.S_IFDIR | 0o755},
            )
            with mock.patch.object(dev, "REPO_ROOT", root), mock.patch.object(
                dev, "_tracked_paths", return_value=[tracked]
            ), mock.patch.object(
                dev, "_project_mount", return_value=mount
            ), mock.patch.object(
                Path, "stat", autospec=True, return_value=owner
            ), mock.patch.object(
                dev.os, "getuid", return_value=uid
            ), mock.patch.object(dev.os, "getgid", return_value=uid):
                yield tmp

    def _reclaim(
        self,
        owner_uid: int,
        uid: int,
        container_user: str | None = None,
    ):
        """Run `_reclaim_checkout` for a temp mount owned by `owner_uid`.

        The container runs as `uid` itself unless `container_user` says
        otherwise (the root case, where it is the image user).
        """
        with self._checkout(owner_uid=owner_uid, uid=uid):
            dev._reclaim_checkout(
                dev.HOST_ROOT,
                dev.CONTAINER_PROJECT_ROOT,
                container_user or f"{uid}:{uid}",
            )

    def test_project_mount_translates_only_inside_the_container(self):
        # Inside the container /project is reached as <HOST_ROOT>/project.
        self.assertEqual(
            dev._project_mount(dev.HOST_ROOT, "/project", Path("/project")),
            Path("/dev/project"),
        )
        # On the host that path does not exist: the source is the checkout,
        # wherever it really lives (the CI failure: /dev/project not found).
        self.assertEqual(
            dev._project_mount(dev.HOST_ROOT, "/project", Path("/home/me/repo")),
            Path("/home/me/repo"),
        )

    def test_owner_is_read_through_the_mount_the_container_sees(self):
        # The host's own view of the checkout can be stale (a bind mount keeps
        # the owner it had when it was mounted), so only the mount point counts.
        with self._checkout(owner_uid=1001, uid=1000):
            source = dev._project_mount(
                dev.HOST_ROOT, dev.CONTAINER_PROJECT_ROOT, dev.REPO_ROOT
            )
            self.assertEqual(dev._checkout_owner_uid(source), 1001)

    def test_unreadable_source_fails_loudly_inside_the_container(self):
        # Inside the container the source is `<host_root>/project`; if it
        # cannot be read, that must raise a `CheckoutError` naming the
        # directory to look at, not escape as a raw `FileNotFoundError`
        # traceback (which is how the CI run once failed).
        with tempfile.TemporaryDirectory() as tmp:
            host_root = str(Path(tmp).resolve())
            with mock.patch.object(
                dev, "REPO_ROOT", Path("/project")
            ), mock.patch.object(
                dev, "_tracked_paths", return_value=[]
            ), mock.patch.object(
                dev, "_container_write_failure", return_value=None
            ):
                with self.assertRaises(dev.CheckoutError) as raised:
                    dev._reclaim_checkout(host_root, "/project", "1000:1000")
        self.assertIn(f"{host_root}/project", str(raised.exception))
        self.assertEqual(self.calls, [])

    def test_host_user_is_passed_through(self):
        # The container runs as the host user, so everything it writes in the
        # bind-mounted project keeps the host ownership.
        self.assertEqual(dev._container_user(1000, 1000), "1000:1000")
        self.assertEqual(dev._container_user(1001, 1001), "1001:1001")
        self.assertEqual(dev._container_user(1001, 1002), "1001:1002")

    def test_root_runs_as_the_image_user(self):
        # Running as root would leave every build artifact root-owned.
        self.assertEqual(
            dev._container_user(0, 0),
            f"{dev.CONTAINER_UID}:{dev.CONTAINER_GID}",
        )

    def test_container_uid_gid_reads_the_user_value(self):
        self.assertEqual(dev._container_uid_gid("1001:1002"), (1001, 1002))
        # A bare uid means the same GID (`docker --user 1001`).
        self.assertEqual(dev._container_uid_gid("1001"), (1001, 1001))

    def test_environment_pins_a_writable_home_and_ccache(self):
        # The image's own user owns its home.
        env = dev._container_environment("1000:1000", {})
        self.assertEqual(env["HOME"], dev.CONTAINER_HOME)
        self.assertTrue(env["CCACHE_DIR"].startswith(dev.CONTAINER_HOME + "/"))
        self.assertEqual(
            env["CCACHE_DIR"], f"{dev.CONTAINER_HOME}/{dev.CCACHE_DIR}"
        )

    def test_other_user_gets_a_writable_home(self):
        # A host-user container has no passwd entry in the image, so there is
        # no home for it; /tmp is writable and holds its own cache directory.
        # Its HOME must never be the image user's home (not writable by it).
        env = dev._container_environment("1001:1001", {})
        self.assertEqual(env["HOME"], "/tmp")
        self.assertEqual(env["CCACHE_DIR"], f"/tmp/{dev.CCACHE_DIR}")
        self.assertNotEqual(env["HOME"], dev.CONTAINER_HOME)

    def test_home_is_not_the_image_home_for_other_users(self):
        for user in ("1001:1001", "0:0", "65534:65534"):
            with self.subTest(user=user):
                env = dev._container_environment(user, {})
                self.assertTrue(env["HOME"].startswith("/tmp"), user)

    def test_environment_trusts_only_the_idf_tree(self):
        # Trusting `*` would hide a genuinely bad project checkout.
        env = dev._container_environment("1000:1000", {})
        self.assertEqual(env["GIT_CONFIG_VALUE_0"], dev.IDF_SAFE_DIRECTORY)
        self.assertEqual(env["GIT_CONFIG_COUNT"], "1")
        self.assertNotEqual(env["GIT_CONFIG_VALUE_0"], "*")

    def test_environment_does_not_mutate_the_base(self):
        base = {"PATH": "/usr/bin"}
        dev._container_environment("1000:1000", base)
        self.assertEqual(base, {"PATH": "/usr/bin"})

    def test_image_user_runs_the_command_through_the_entrypoint(self):
        # The image user owns the login files, so a login shell has already
        # set up ESP-IDF; the command still goes through `sh -c` under the
        # image entrypoint (`CONTAINER_SHELL`), never bare and never through
        # an entrypoint override.
        self.assertEqual(
            dev._run_command("1000:1000", ["idf.py", "--version"]),
            [
                "docker", "compose", "run",
                "--user", "1000:1000",
                "--rm",
                "dev", *dev.CONTAINER_SHELL, "idf.py --version",
            ],
        )

    def test_other_user_sources_the_idf_environment_first(self):
        # A uid the image does not know cannot read the image home, so its
        # login files (which source ESP-IDF) never run: source them explicitly
        # in the same shell `-c` string, or `idf.py` is not found.
        cmd = dev._run_command("1001:1001", ["idf.py", "-C", "mcgowan", "build"])
        self.assertEqual(
            cmd[:7],
            ["docker", "compose", "run", "--user", "1001:1001", "--rm", "dev"],
        )
        self.assertEqual(cmd[7:9], dev.CONTAINER_SHELL)
        self.assertEqual(
            cmd[9],
            f". {dev.IDF_EXPORT}{dev.COMMAND_SEPARATOR}idf.py -C mcgowan build",
        )

    def test_no_command_overrides_the_image_entrypoint(self):
        # The image entrypoint (`exec "$@"`) runs what it is given. A
        # `--entrypoint …` override is not exec'd the same way, so it must
        # never appear in the argv.
        for user in ("1000:1000", "1001:1001"):
            with self.subTest(user=user):
                cmd = dev._run_command(user, ["dev.py", "build"])
                self.assertNotIn("--entrypoint", cmd)
                self.assertIn("dev", cmd)
                self.assertEqual(cmd[cmd.index("dev") + 1], "sh")

    def test_script_is_an_argument_of_sh_not_of_the_source_command(self):
        # Regression for the CI exit 127:
        #   /project/. $IDF_PATH/export.sh; scripts/dev.py build: No such file
        # Passing `dev -c <script>` *replaces* the image CMD with the arguments
        # and the entrypoint runs `/bin/sh -c <script>`; `/bin/sh` is dash,
        # which does not accept arguments after `.`, so `. <export>; <cmd>` was
        # taken as one file name. With `sh -c <script>` the script is an
        # argument of `sh`, never an argument of `.`.
        for user in ("1000:1000", "1001:1001"):
            for args in (["dev.py", "build"], ["idf.py", "--version"]):
                with self.subTest(user=user, args=args):
                    cmd = dev._run_command(user, args)
                    self.assertEqual(cmd[-2:-1], ["-c"])
                    self.assertEqual(cmd[-3:-2], ["sh"])
                    script = cmd[-1]
                    # `.` must see exactly one argument (the export file) --
                    # or, for the image's own user, not be used at all (its
                    # login shell already sourced ESP-IDF).
                    if user == "1000:1000":
                        self.assertNotIn(". ", script)
                    else:
                        source = script.split(dev.COMMAND_SEPARATOR.strip())[0]
                        self.assertEqual(source, f". {dev.IDF_EXPORT}")

    def test_write_probe_script_is_an_argument_of_sh(self):
        # The probe takes the same shape as every other command: without `sh`
        # the entrypoint would hand the script to dash (see
        # `test_script_is_an_argument_of_sh_not_of_the_source_command`).
        self.reply[0] = 0
        self._reclaim(owner_uid=1001, uid=1001)
        probe = [cmd for cmd, _ in self.calls if "compose" in cmd][0]
        self.assertEqual(probe[-3:-1], ["sh", "-c"])
        self.assertEqual(probe[6:8], ["dev", "sh"])

    def test_other_user_script_separates_the_source_from_the_command(self):
        # Regression: `.` treats every following word as an argument to the
        # script being sourced, so `. $X cmd` looked for a script named
        # "$X cmd" and the build died with exit 127 ("No such file or
        # directory"). The command must be a separate statement.
        script = dev._run_command("1001:1001", ["dev.py", "build"])[-1]
        self.assertNotIn(f"{dev.IDF_EXPORT} dev.py", script)
        self.assertIn(f"{dev.IDF_EXPORT}; dev.py build", script)

    def test_other_user_arguments_are_shell_quoted(self):
        # The arguments become a shell command line, so they must survive
        # shell metacharacters intact; no `exec`/`"$@"` (args are not argv).
        cmd = dev._run_command("1001:1001", ["sh", "-c", "echo hi | wc -l"])
        self.assertIn("'echo hi | wc -l'", cmd[-1])
        self.assertNotIn("exec", cmd[-1])
        self.assertNotIn("$@", cmd[-1])

    def test_root_gets_the_image_user_command_shape(self):
        # `compose_run` maps root onto the image user, whose home is readable.
        container_user = dev._container_user(0, 0)
        with redirect_stderr(io.StringIO()):
            cmd = dev._run_command(container_user, ["dev.py", "build"])
        self.assertEqual(cmd[6:], ["dev", *dev.CONTAINER_SHELL, "dev.py build"])

    def test_only_writing_commands_write_the_project(self):
        for name in dev.WRITING_COMMANDS:
            self.assertTrue(dev._command_writes_project(["dev.py", name]), name)
        for name in ("pull", "self-check", "shell", "monitor", "complete"):
            self.assertFalse(
                dev._command_writes_project(["dev.py", name]), name
            )
        self.assertFalse(dev._command_writes_project(["dev.py"]))

    def test_matching_host_user_needs_no_chown(self):
        # The normal developer and CI case: the host user owns the checkout
        # and the container runs as it.
        for uid, gid in ((1000, 1000), (1000, 1001), (1001, 1001)):
            with self.subTest(uid=uid, gid=gid):
                self.assertIsNone(
                    dev._checkout_transfer_command(uid, gid, uid, uid, gid)
                )

    def test_foreign_checkout_is_given_to_the_container_user(self):
        # Running as root, the container is the image user, but the checkout
        # under /home/runner may belong to 1001: hand it over.
        cmd = dev._checkout_transfer_command(0, 0, 1001, 1000, 1000)
        self.assertEqual(
            cmd[:4], ["find", str(dev.REPO_ROOT), "-xdev", "-exec"]
        )
        self.assertIn(f"{dev.CONTAINER_UID}:{dev.CONTAINER_GID}", cmd)
        self.assertEqual(cmd[-1], "+")

    def test_chown_from_root_does_not_leave_group_zero(self):
        # A sudo run must not make the checkout group root.
        cmd = dev._checkout_transfer_command(0, 0, 0, 1000, 1000)
        self.assertIn(f"{dev.CONTAINER_UID}:{dev.CONTAINER_GID}", cmd)
        cmd = dev._checkout_transfer_command(0, 0, 0, 1001, 1002)
        self.assertIn("1001:1002", cmd)

    def test_non_owner_is_told_to_use_sudo(self):
        # A checkout owned by a third party cannot be handed over. This must
        # fail loudly (never silently continue), since CI once showed a
        # warning path degrade to "no output, build anyway".
        with self.assertRaises(dev.CheckoutError) as raised:
            self._reclaim(owner_uid=1002, uid=1001, container_user="1000:1000")
        self.assertIn("sudo", str(raised.exception))
        self.assertEqual(self.calls, [])

    def test_host_user_checkout_is_never_chowned(self):
        # The GitHub-runner case after the fix: the checkout belongs to the
        # host user (1001) and the container runs as that same user, so no
        # chown is attempted (a non-root user cannot give files to 1000
        # anyway: "chown: Operation not permitted") and only the probe runs.
        self._reclaim(owner_uid=1001, uid=1001)
        self.assertEqual([cmd for cmd, _ in self.calls if cmd[0] == "find"], [])
        self.assertEqual(len(self.calls), 1)  # just the probe
        self.assertIn("1001:1001", self.calls[0][0])

    def test_root_checkout_is_given_to_the_image_user(self):
        # dev.py as root with the container as the image user: the chown runs,
        # and the probe then runs as that user, verifying the hand-over.
        with self._checkout(owner_uid=1001, uid=0) as tmp:
            with redirect_stderr(io.StringIO()) as err:
                dev._reclaim_checkout(
                    dev.HOST_ROOT, dev.CONTAINER_PROJECT_ROOT, "1000:1000"
                )
        self.assertIn("giving the checkout", err.getvalue())
        chowns = [cmd for cmd, _ in self.calls if cmd[0] == "find"]
        self.assertEqual(len(chowns), 1)
        self.assertEqual(chowns[0][:4], ["find", str(tmp), "-xdev", "-exec"])
        self.assertIn(f"{dev.CONTAINER_UID}:{dev.CONTAINER_GID}", chowns[0])
        probes = [cmd for cmd, _ in self.calls if "compose" in cmd]
        self.assertEqual(len(probes), 1)
        self.assertEqual(probes[0][5], "1000:1000")
        # The probe must write inside the container's mount, not host paths.
        self.assertIn("/project/mcgowan/.dev-py-write-probe", probes[0][-1])

    def test_unwritable_checkout_raises_instead_of_continuing(self):
        # chown can "succeed" and transfer nothing (a stale bind mount); the
        # probe must turn that into a loud error, never a silent continue.
        self.reply[0] = 1  # the probe fails
        with redirect_stderr(io.StringIO()):
            with self.assertRaises(dev.CheckoutError):
                self._reclaim(owner_uid=1001, uid=0, container_user="1000:1000")

    def test_write_probe_only_creates_and_removes_the_probe(self):
        # It must never write an existing file, and its cleanup must never be
        # able to delete anything but the probe itself (a probe that touched
        # and `rm -f`ed every tracked file once left a checkout destroyed).
        targets = [Path("/project/mcgowan"), Path("/project")]
        script = dev._container_write_probe_script(
            Path("/project/.probe"), targets
        )
        self.assertEqual(
            script,
            "touch /project/mcgowan/.probe /project/.probe && "
            "rm -f /project/mcgowan/.probe /project/.probe",
        )
        touched = script.partition("&&")[0].split()[1:]
        removed = script.partition("&&")[2].split()[2:]
        self.assertEqual(touched, removed)
        for path in touched:
            self.assertTrue(path.endswith("/.probe"), path)

    def test_write_probe_without_targets_probes_the_mount_root(self):
        # Never emit `touch && rm -f`, which is a shell syntax error.
        script = dev._container_write_probe_script(Path("/project/.probe"), [])
        self.assertEqual(
            script,
            "touch /project/.probe && rm -f /project/.probe",
        )

    def test_checkout_owned_by_the_container_user_needs_no_chown(self):
        # A mount the image user owns cannot be chowned by a non-root host
        # user, but needs no chown: the write probe decides. Never fail here.
        self._reclaim(owner_uid=dev.CONTAINER_UID, uid=1001, container_user="1000:1000")
        self.assertEqual([cmd for cmd, _ in self.calls if cmd[0] == "find"], [])
        self.assertEqual(len(self.calls), 1)  # just the probe

    def test_write_probe_runs_under_the_image_entrypoint(self):
        # Regression: `--entrypoint sh` makes compose hand the `-c` script to
        # the image entrypoint as ONE exec argument, so `exec` looked for a
        # file literally named "touch … && rm -f …" (exit 127). The probe must
        # be the command instead, so the entrypoint runs it normally.
        self.reply[0] = 0
        self._reclaim(owner_uid=dev.CONTAINER_UID, uid=1001, container_user="1000:1000")
        probe = [cmd for cmd, _ in self.calls if "compose" in cmd][0]
        self.assertNotIn("--entrypoint", probe)
        self.assertEqual(probe[6:], ["dev", *dev.CONTAINER_SHELL, probe[-1]])

    def test_compose_run_runs_as_the_host_user(self):
        # The GitHub-runner shape (uid 1001 owning the checkout): the
        # container runs as that user, so nothing is chowned and only the
        # write probe runs before the real command.
        with self._checkout(owner_uid=1001, uid=1001):
            self.assertEqual(dev.compose_run(["dev.py", "format"]), 0)
        self.assertEqual([cmd for cmd, _ in self.calls if cmd[0] == "find"], [])
        self.assertEqual(len(self.calls), 2)  # probe, then the real run
        self.assertIn("touch ", self.calls[0][0][-1])
        # A uid the image does not know gets the command through a shell that
        # sources the ESP-IDF environment first (see `_run_command`).
        self.assertEqual(
            self.calls[-1][0],
            [
                "docker", "compose", "run", "--user", "1001:1001", "--rm",
                "dev", *dev.CONTAINER_SHELL,
                f". {dev.IDF_EXPORT}{dev.COMMAND_SEPARATOR}dev.py format",
            ],
        )

    def test_compose_run_skips_the_chown_when_not_writing(self):
        # A checkout the container user owns: nothing to chown.
        with self._checkout(owner_uid=dev.CONTAINER_UID, uid=1001):
            with redirect_stderr(io.StringIO()):
                self.assertEqual(dev.compose_run(["dev.py", "shell", "ls"]), 0)
        self.assertEqual([cmd for cmd, _ in self.calls if cmd[0] == "find"], [])
        self.assertEqual(len(self.calls), 1)

    def test_compose_run_fails_before_building_an_unwritable_checkout(self):
        # The container cannot write the mount: never start the build.
        self.reply[0] = 1  # the probe container exits non-zero
        with self._checkout(owner_uid=1001, uid=1001):
            with redirect_stderr(io.StringIO()) as err:
                self.assertEqual(dev.compose_run(["dev.py", "build"]), 1)
        self.assertIn("cannot write the project mount", err.getvalue())
        # Only the probe ran; the build itself was never started.
        self.assertEqual(len(self.calls), 1)
        self.assertIn("touch ", self.calls[0][0][-1])

    def test_compose_run_passes_user_and_environment(self):
        # A checkout the container user owns: no chown, only the probe.
        with self._checkout(owner_uid=dev.CONTAINER_UID, uid=dev.CONTAINER_UID):
            self.assertEqual(dev.compose_run(["dev.py", "build"]), 0)
        cmd, env = self.calls[-1]
        self.assertEqual(
            cmd[:6],
            ["docker", "compose", "run", "--user",
             f"{dev.CONTAINER_UID}:{dev.CONTAINER_GID}", "--rm"],
        )
        self.assertEqual(cmd[6:], ["dev", *dev.CONTAINER_SHELL, "dev.py build"])
        self.assertEqual(env["HOME"], dev.CONTAINER_HOME)
        self.assertEqual(env["GIT_CONFIG_VALUE_0"], dev.IDF_SAFE_DIRECTORY)

    def test_dockerfile_image_carries_the_same_safety_net(self):
        # Direct `idf.py` use (docs, CLion) bypasses dev.py, so the image must
        # pin the same HOME/CCACHE_DIR/safe.directory.
        dockerfile = (dev.REPO_ROOT / "Dockerfile").read_text()
        self.assertIn("safe.directory /opt/esp/idf", dockerfile)
        self.assertIn("CCACHE_DIR=/home/${USERNAME}/.cache/ccache", dockerfile)
        self.assertNotIn("safe.directory '*'", dockerfile)
        self.assertNotIn('safe.directory "*"', dockerfile)


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

    def test_self_check_runs_on_the_host_without_docker(self):
        # CI runs `dev.py self-check` on a fresh runner (before any container
        # exists), so it must never hop into the container; it just runs
        # `python3 -m unittest <SELF_CHECK_MODULE>` from the repo root.
        calls = []
        original_run = dev.run
        dev.run = lambda cmd, cwd=dev.REPO_ROOT: calls.append(list(cmd)) or 0
        try:
            self.assertEqual(dev.cmd_self_check(), 0)
        finally:
            dev.run = original_run
        self.assertEqual(
            calls,
            [["python3", "-m", "unittest", "-v", dev.SELF_CHECK_MODULE]],
        )


if __name__ == "__main__":
    unittest.main()
