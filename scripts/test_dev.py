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
import json
import os
from pathlib import Path
import re
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


class IdfRunnerTests(unittest.TestCase):
    """The aggregate targets reach `idf.py` through `scripts/idf.sh`.

    The image's entrypoint sources `export.sh`, but nothing started *later*
    (CLion's CMake/Ninja build in particular) inherits that environment, so a
    bare `idf.py` there dies with "TypeError: ... got 'NoneType'" because
    `ESP_IDF_VERSION` is unset. The CMake targets must therefore go through the
    wrapper, which establishes the environment itself; see scripts/idf.sh.
    """

    RUNNER = "scripts/idf.sh"

    def _runner(self) -> Path:
        return dev.REPO_ROOT / self.RUNNER

    def test_runner_exists_and_is_executable(self):
        runner = self._runner()
        self.assertTrue(runner.is_file(), f"missing {runner}")
        self.assertTrue(
            os.access(runner, os.X_OK),
            f"runner is not executable: {runner}",
        )

    def test_runner_is_tracked_by_git(self):
        tracked = subprocess.run(
            ("git", "ls-files", "--", self.RUNNER),
            cwd=dev.REPO_ROOT,
            capture_output=True,
            text=True,
            check=True,
        ).stdout.splitlines()
        self.assertIn(self.RUNNER, tracked)

    def test_runner_establishes_the_environment_when_absent(self):
        script = self._runner().read_text()
        # idf.py needs both of these; export.sh is what provides them.
        self.assertIn("IDF_PYTHON_ENV_PATH", script)
        self.assertIn("ESP_IDF_VERSION", script)
        self.assertIn("export.sh", script)
        # ...and it must fail loudly rather than exec into a broken env.
        self.assertIn("exit 1", script)

    def test_runner_passes_the_command_through_unmodified(self):
        script = self._runner().read_text()
        self.assertIn('exec "$@"', script)

    def test_aggregate_targets_use_the_runner(self):
        # Every `<subproject>_build` target runs idf.py via the wrapper, never
        # the bare binary: that is what makes an IDE-launched build work.
        root_cmake = (dev.REPO_ROOT / "CMakeLists.txt").read_text()
        self.assertIn("set(IDF_RUNNER", root_cmake)
        self.assertIn('"${IDF_RUNNER}"', root_cmake)
        # The runner is referenced from the shared helper, so no target can
        # accidentally bypass it.
        helper = root_cmake.split("function(_idf_subproject", 1)[1]
        self.assertIn('"${IDF_RUNNER}"', helper.split("endfunction()", 1)[0])


class ClionRunConfigurationTests(unittest.TestCase):
    """CLion CMake run configurations name the root CMake project.

    A CMake run configuration is bound to one CMake project by
    `PROJECT_NAME`, which must be the name the root `project()` declares.
    The committed value went stale when the firmware moved into `mcgowan/`
    and the root became `project(esp32_sensors_subprojects NONE)`; a stale
    name is one of the ways CLion reports "cannot find any profile" / no
    target. CLion rewrites the attribute itself once it resolves the project,
    which is why leaving it stale is a latent breakage rather than a fix.

    Only `PROJECT_NAME` is checked: the comments and the trailing newline are
    also lost on a CLion rewrite, and the tracked copies are the ones that
    should keep them.
    """

    def _root_project_name(self) -> str:
        """The single `project()` name declared by the root CMakeLists.txt."""
        root_cmake = (dev.REPO_ROOT / "CMakeLists.txt").read_text()
        names = re.findall(r"^project\(\s*([A-Za-z0-9_]+)", root_cmake, re.M)
        self.assertEqual(
            len(names), 1, f"root CMakeLists.txt declares project() {names}"
        )
        return names[0]

    def _cmake_run_configurations(self):
        """(filename, element) for every CMake run configuration."""
        import xml.etree.ElementTree as ET

        directory = dev.REPO_ROOT / ".idea" / "runConfigurations"
        self.assertTrue(directory.is_dir(), f"missing {directory}")
        found = []
        for path in sorted(directory.glob("*.xml")):
            root = ET.parse(path).getroot()
            for configuration in root.iter("configuration"):
                if configuration.get("type") == "CMakeRunConfiguration":
                    found.append((path.name, configuration))
        self.assertTrue(found, "no CMake run configurations found")
        return found

    def test_every_cmake_run_configuration_names_the_root_project(self):
        expected = self._root_project_name()
        for name, configuration in self._cmake_run_configurations():
            with self.subTest(configuration=name):
                self.assertEqual(configuration.get("PROJECT_NAME"), expected)

    def test_cmake_run_configurations_name_an_existing_profile(self):
        # CONFIG_NAME selects the profile that supplies the build directory and
        # toolchain, so a name that is not in .idea/cmake.xml fails the run.
        cmake_xml = (dev.REPO_ROOT / ".idea" / "cmake.xml").read_text()
        profiles = set(re.findall(r'PROFILE_NAME="([^"]+)"', cmake_xml))
        self.assertTrue(profiles, ".idea/cmake.xml declares no profiles")
        for name, configuration in self._cmake_run_configurations():
            with self.subTest(configuration=name):
                self.assertIn(configuration.get("CONFIG_NAME"), profiles)


class DevImageTests(unittest.TestCase):
    """The pinned prebuilt image is wired up consistently.

    The dev image is pulled, not built: `docker-compose.yml`, `dev.py`
    (`DEV_IMAGE`) and CI must all name the same tag, or CI would run a
    different toolchain than a developer does.
    """

    def test_dev_image_is_the_pinned_prebuilt_tag(self):
        self.assertEqual(dev.DEV_IMAGE, "marcpawl/esp-idf:20261025")

    def test_compose_pins_the_same_image(self):
        compose = (dev.REPO_ROOT / "docker-compose.yml").read_text()
        self.assertIn(f"image: {dev.DEV_IMAGE}", compose)

    def test_compose_pulls_instead_of_rebuilding(self):
        # `pull_policy: missing` fetches the pinned image from the registry; a
        # `build:` section alone would let compose rebuild it silently.
        compose = (dev.REPO_ROOT / "docker-compose.yml").read_text()
        self.assertIn("pull_policy: missing", compose)

    def test_ci_pulls_the_image_and_does_not_build_it(self):
        ci = (dev.REPO_ROOT / ".github" / "workflows" / "ci.yml").read_text()
        self.assertIn("dev.py pull", ci)
        # Building the image in CI would defeat the pinned prebuilt image.
        self.assertNotIn("dev.py image", ci)
        self.assertNotIn("setup-buildx-action", ci)

    def test_dockerfile_is_kept_so_the_image_can_be_rebuilt(self):
        # The image is prebuilt, but the Dockerfile stays reproducible.
        self.assertTrue((dev.REPO_ROOT / "Dockerfile").is_file())
        compose = (dev.REPO_ROOT / "docker-compose.yml").read_text()
        self.assertIn("dockerfile: Dockerfile", compose)

    def test_pull_is_registered_and_host_only(self):
        self.assertIn("pull", dev.COMMANDS)
        self.assertIs(dev.COMMANDS["pull"], dev.cmd_pull)

    def test_dev_image_tag_is_date_stamped(self):
        # Published tags are date-stamped and never moved, so a rebuild is
        # always published under a new tag rather than overwriting a pinned
        # one that CI and every developer already reference.
        repository, _, tag = dev.DEV_IMAGE.partition(":")
        self.assertEqual(repository, "marcpawl/esp-idf")
        self.assertRegex(tag, r"^\d{8}$")

    def test_dockerfile_extends_the_official_espressif_image(self):
        # The pinned image is the official ESP-IDF v6.1 image plus project
        # tooling; AGENTS.md's toolchain note relies on this.
        dockerfile = (dev.REPO_ROOT / "Dockerfile").read_text()
        self.assertIn("FROM espressif/idf:release-v6.1", dockerfile)

    def test_local_image_is_not_the_pinned_tag(self):
        # `image` (a local rebuild) must not overwrite the pinned tag, which
        # would silently swap the toolchain under a developer.
        self.assertNotEqual(dev.LOCAL_IMAGE, dev.DEV_IMAGE)


class DevContainerTests(unittest.TestCase):
    """`.devcontainer/devcontainer.json` matches the prebuilt image.

    The Dev Container reuses the compose service, so it must not contradict
    what the pinned image provides: the image keeps its tools in /opt/esp (not
    the classic per-user `~/.espressif`), and it already has the toolchain and
    Python venv installed, so there is nothing to install at create time.
    """

    @staticmethod
    def _strip_jsonc(text: str) -> str:
        """Drop `//` comments and trailing commas so `json` can parse it.

        `devcontainer.json` is JSONC. Only whole-line `//` comments are
        stripped, which is all this file uses and all `//` inside string
        values (there are none here) would break.
        """
        without_comments = re.sub(r"^\s*//.*$", "", text, flags=re.MULTILINE)
        return re.sub(r",(\s*[}\]])", r"\1", without_comments)

    def _devcontainer(self) -> dict:
        """The parsed devcontainer.json (so assertions ignore comments)."""
        path = dev.REPO_ROOT / ".devcontainer" / "devcontainer.json"
        return json.loads(self._strip_jsonc(path.read_text()))

    def test_tools_path_matches_the_image(self):
        # The image sets IDF_TOOLS_PATH=/opt/esp; pointing IDE processes at
        # ~/.espressif would make them look in an empty directory.
        self.assertEqual(
            self._devcontainer()["remoteEnv"]["IDF_TOOLS_PATH"],
            "/opt/esp",
        )

    def test_does_not_reinstall_into_a_prebuilt_image(self):
        # `install.sh` / `install-python-env` are baked into the image already;
        # running them per container create only adds latency.
        command = self._devcontainer()["postCreateCommand"]
        self.assertNotIn("install-python-env", command)
        self.assertNotIn("install.sh", command)

    def test_post_create_verifies_the_toolchain(self):
        # Whatever the create hook runs, it must fail loudly if the pinned
        # image is missing or the ESP-IDF environment cannot be sourced.
        self.assertIn("idf.py --version", self._devcontainer()["postCreateCommand"])

    def test_uses_the_compose_dev_service(self):
        # The Dev Container must not define its own image: it inherits the
        # pinned one from docker-compose.yml.
        text = self._devcontainer()
        self.assertEqual(text["dockerComposeFile"], "../docker-compose.yml")
        self.assertEqual(text["service"], "dev")
        self.assertNotIn("image", text)
        self.assertNotIn("build", text)


class ContainerUserTests(unittest.TestCase):
    """The container runs as the calling user, with a usable HOME.

    A bind mount does not translate uids: whatever uid the container runs as is
    the uid the host sees on the files it writes. So the container must run as
    the calling user (no chown, no userns remapping), and a uid that has no
    passwd entry inside the image needs HOME/CCACHE_DIR pinned, or ccache fails
    with "ccache: error: Permission denied" (which is what broke GitHub CI,
    whose runner is uid 1001).
    """

    def test_container_user_is_the_calling_user(self):
        # Not the image's uid: the files the container writes land on the host
        # with this uid, and they must belong to whoever ran the command.
        self.assertEqual(
            dev._container_user(),
            f"{os.getuid()}:{os.getgid()}",
        )

    def test_image_user_keeps_the_image_home(self):
        # uid 1000 owns /home/dev, so nothing is overridden and the persisted
        # cache volume there keeps working.
        self.assertEqual(dev._container_environment("1000:1000"), [])

    def test_other_users_get_a_writable_home_and_ccache_dir(self):
        # A GitHub runner (1001) has no passwd entry, so $HOME is "/" and
        # ccache's cache dir becomes unusable.
        self.assertEqual(
            dev._container_environment("1001:1001"),
            ["-e", "HOME=/tmp", "-e", "CCACHE_DIR=/tmp/.cache/ccache"],
        )

    def test_ccache_dir_is_inside_the_pinned_home(self):
        flags = dev._container_environment("4242:4242")
        home = flags[flags.index("HOME=/tmp")].split("=", 1)[1]
        ccache = flags[flags.index("CCACHE_DIR=/tmp/.cache/ccache")]
        self.assertTrue(ccache.split("=", 1)[1].startswith(f"{home}/"))

    def test_compose_run_runs_as_the_calling_user(self):
        calls = []
        original = dev.run
        try:
            dev.run = lambda cmd, cwd=dev.REPO_ROOT: calls.append(list(cmd)) or 0
            self.assertEqual(dev.compose_run(["dev.py", "build"]), 0)
        finally:
            dev.run = original
        command = calls[0]
        self.assertEqual(command[:4], ["docker", "compose", "run", "--user"])
        self.assertEqual(command[4], dev._container_user())
        self.assertIn("--rm", command)
        # The re-executed command follows the service name.
        self.assertEqual(command[-2:], ["dev.py", "build"])

    def test_compose_run_passes_home_flags_before_the_service_name(self):
        # Independent of the uid running the tests: force the CI (non-1000)
        # environment so the `-e` flags are always present, then check they are
        # compose options (before the service name), not part of the command.
        calls = []
        original_run, original_env = dev.run, dev._container_environment
        try:
            dev.run = lambda cmd, cwd=dev.REPO_ROOT: calls.append(list(cmd)) or 0
            dev._container_environment = lambda user: ["-e", "HOME=/tmp"]
            dev.compose_run(["dev.py", "build"])
        finally:
            dev.run, dev._container_environment = original_run, original_env
        command = calls[0]
        # Everything after the service name belongs to the command.
        self.assertLess(command.index("-e"), command.index("dev"))
        self.assertLess(command.index("--rm"), command.index("dev"))
        self.assertEqual(command[command.index("dev") + 1:], ["dev.py", "build"])


class SpecRevisionTests(unittest.TestCase):
    """`Specification.md` is the source of truth; docs must cite its revision.

    AGENTS.md names the spec revision in two places, and the spec's own change
    history must end at that revision. They drifted before (the spec was at
    Rev 12 while its readers assumed a later one), so this pins them together.
    """

    def _spec(self) -> str:
        return (dev.REPO_ROOT / "Specification.md").read_text()

    def _revision(self) -> int:
        match = re.search(r"^\*\*Version:\*\* Rev (\d+)$", self._spec(), re.M)
        self.assertIsNotNone(match, "Specification.md has no '**Version:** Rev N'")
        return int(match.group(1))

    def test_spec_header_and_change_history_agree(self):
        # The last change-history row must be the revision in the header.
        rows = re.findall(r"^\| (\d+) \| ", self._spec(), re.M)
        self.assertTrue(rows, "Specification.md has no change-history table")
        self.assertEqual(int(rows[-1]), self._revision())

    def test_agents_cites_the_current_revision(self):
        agents = (dev.REPO_ROOT / "AGENTS.md").read_text()
        revision = self._revision()
        self.assertIn(f"software specification (Rev {revision})", agents)
        self.assertIn(f"Authoritative software spec (Rev {revision})", agents)

    def test_spec_names_the_pinned_image(self):
        # The spec's environment table must not contradict the pinned image.
        self.assertIn(dev.DEV_IMAGE, self._spec())


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
