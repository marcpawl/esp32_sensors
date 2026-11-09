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
import shutil
import subprocess
import sys
import tempfile
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


class SubmoduleTests(unittest.TestCase):
    """The vendored `sml` submodule and the commands that fetch and skip it.

    `third_party/sml` is a git submodule pinned to boost-ext/sml v1.2.0 and a
    requirement of the `logic` component. A bare clone leaves it empty, so
    `build`/`test`/`clang-tidy` fetch it and CI runs `submodules` on its own.
    Its sources are vendored: never formatted, never analyzed.
    """

    SML_PATH = "third_party/sml"
    SML_URL = "https://github.com/boost-ext/sml.git"
    # The commit tag v1.2.0 points at; the gitlink must record exactly this.
    SML_COMMIT = "2d8485da6661519a7697b5f800c7165c543fa499"

    def test_sml_is_a_registered_submodule(self):
        self.assertIn(self.SML_PATH, dev.SUBMODULES)

    def test_every_submodule_has_a_marker_inside_it(self):
        for submodule in dev.SUBMODULES:
            with self.subTest(submodule=submodule):
                marker = dev.SUBMODULE_MARKERS[submodule]
                self.assertTrue(
                    marker.startswith(submodule + "/"),
                    f"marker {marker!r} is outside {submodule}",
                )

    def test_gitmodules_records_the_submodule_url(self):
        gitmodules = (dev.REPO_ROOT / ".gitmodules").read_text()
        self.assertIn(f"path = {self.SML_PATH}", gitmodules)
        self.assertIn(f"url = {self.SML_URL}", gitmodules)

    def test_sml_is_pinned_to_the_v1_2_0_commit(self):
        # A gitlink (mode 160000) records the exact commit, so the pin is a tag
        # that resolves to one commit rather than a branch that moves. Read the
        # index (`ls-files -s`), which is what a commit records and which is
        # already correct before the submodule is committed.
        entry = subprocess.run(
            ("git", "ls-files", "-s", "--", self.SML_PATH),
            cwd=dev.REPO_ROOT,
            capture_output=True,
            text=True,
            check=True,
        ).stdout.split()
        self.assertEqual(entry[0], "160000", entry)
        self.assertEqual(entry[1], self.SML_COMMIT, entry)

    def test_missing_submodules_reports_an_unfetched_tree(self):
        # A bare clone has the gitlink but no marker file, so the fetch path
        # must fire; with the marker present it must not.
        markers = {self.SML_PATH: f"{self.SML_PATH}/include/boost/sml.hpp"}
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            self.assertEqual(
                dev._missing_submodules(markers, root=root), [self.SML_PATH]
            )
            marker = root / markers[self.SML_PATH]
            marker.parent.mkdir(parents=True)
            marker.write_text("// sml\n")
            self.assertEqual(dev._missing_submodules(markers, root=root), [])

    def test_update_command_init_recurses_over_the_named_paths(self):
        self.assertEqual(
            dev._submodule_update_command([self.SML_PATH]),
            [
                "git", "submodule", "update", "--init", "--recursive", "--",
                self.SML_PATH,
            ],
        )

    def test_submodules_command_is_registered_and_documented(self):
        self.assertIn("submodules", dev.COMMANDS)
        self.assertIs(dev.COMMANDS["submodules"], dev.cmd_submodules)
        self.assertTrue((dev.cmd_submodules.__doc__ or "").strip())

    def _function_body(self, name):
        source = (dev.REPO_ROOT / "scripts" / "dev.py").read_text()
        return source.split(f"def {name}", 1)[1].split("\ndef ", 1)[0]

    def test_commands_fetch_missing_submodules(self):
        # A fresh clone must build/test/lint without a manual submodule step,
        # so each entry point checks first.
        for name in (
            "cmd_build",
            "cmd_build_espidf_cpp_test",
            "_run_host_tests",
            "cmd_clang_tidy",
        ):
            with self.subTest(function=name):
                self.assertIn("_ensure_submodules()", self._function_body(name))

    def test_submodules_command_does_not_hop_into_a_container(self):
        # `git submodule update` runs on whichever side invoked dev.py, so the
        # vendored sources land in the real working tree either way.
        self.assertIn("change_to_container()", self._function_body("cmd_submodules"))

    def test_ci_fetches_submodules_before_building(self):
        workflow = (
            dev.REPO_ROOT / ".github" / "workflows" / "ci.yml"
        ).read_text()
        fetch = workflow.index("python3 ./scripts/dev.py submodules")
        build = workflow.index("python3 ./scripts/dev.py build")
        self.assertLess(fetch, build)

    def test_vendored_sources_are_never_analyzed_or_formatted(self):
        # clang-tidy is seeded from the compile database (not ROOTS), so it
        # needs an explicit guard; clang-format discovers files under ROOTS,
        # and `third_party/` is not one of them.
        self.assertTrue(
            dev._is_vendored(Path(f"{self.SML_PATH}/include/boost/sml.hpp"))
        )
        self.assertFalse(dev._is_vendored(Path("components/sml/CMakeLists.txt")))
        self.assertFalse(
            dev._is_vendored(Path("components/logic/include/logic/mode.hpp"))
        )
        self.assertNotIn("third_party", dev.ROOTS)

    def test_component_wraps_the_submodule_headers(self):
        # ESP-IDF needs a component with `idf_component_register`; the upstream
        # tree must never be one, so a thin wrapper points at its headers and
        # `logic` requires that wrapper.
        wrapper = (
            dev.REPO_ROOT / "components" / "sml" / "CMakeLists.txt"
        ).read_text()
        self.assertIn("idf_component_register", wrapper)
        self.assertIn(f"{self.SML_PATH}/include", wrapper)
        logic = (
            dev.REPO_ROOT / "components" / "logic" / "CMakeLists.txt"
        ).read_text()
        self.assertIn("sml", logic.split("REQUIRES", 1)[1])


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

    def _root_targets(self) -> set[str]:
        """Targets the root aggregator defines.

        A CMake run configuration resolves TARGET_NAME against the project in
        PROJECT_NAME, which is the root aggregator - not against the
        subproject whose sources are being built. The root declares one
        `add_custom_target(<sub>_build)` per subproject (via
        `_idf_subproject`) and nothing else, so those `*_build` names are the
        only ones resolvable from a run configuration.
        """
        root_cmake = (dev.REPO_ROOT / "CMakeLists.txt").read_text()
        # `add_custom_target(${sub}_build ALL ...)` is the one target statement.
        self.assertIn("add_custom_target(${sub}_build", root_cmake)
        # The concrete names come from the `_idf_subproject(<name> ...)` calls.
        return {
            f"{sub}_build"
            for sub in re.findall(
                r"^\s*_idf_subproject\(\s*([A-Za-z0-9_-]+)", root_cmake, re.M
            )
        }

    def test_cmake_run_configurations_name_an_existing_root_target(self):
        # Regression: the configurations used to name the subprojects' ESP-IDF
        # ELF targets (mcgowan.elf, donny.elf, ...). Those belong to the
        # subprojects' own CMake projects, so they are invisible from the root
        # aggregator that PROJECT_NAME binds to, and CLion fails with
        # "cannot find target" instead of building.
        root = self._root_targets()
        self.assertTrue(root, "root CMakeLists.txt defines no *_build targets")
        for name, configuration in self._cmake_run_configurations():
            with self.subTest(configuration=name):
                self.assertIn(
                    configuration.get("TARGET_NAME"),
                    root,
                    f"{name}: TARGET_NAME "
                    f"{configuration.get('TARGET_NAME')!r} is not a target of "
                    f"{configuration.get('PROJECT_NAME')!r} (has {sorted(root)})",
                )


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


class TidyCompileArgumentsTests(unittest.TestCase):
    """`_tidy_compile_arguments` launders a GCC command line for clang.

    The ESP-IDF compile database records Xtensa GCC command lines, which the
    Espressif clang driver rejects (or warns about, and the gate promotes
    warnings to errors). These tests pin what is dropped and — just as
    important — what must survive.
    """

    def test_drops_the_compiler_from_argv_zero(self):
        args = dev._tidy_compile_arguments(["gcc", "-DFOO=1", "a.cpp"])
        self.assertEqual(args, ["-DFOO=1", "a.cpp"])

    def test_keeps_the_source_file(self):
        # clang derives the compilation job from the input file: without it the
        # driver reports "no input files" and every translation unit fails.
        args = dev._tidy_compile_arguments(["gcc", "-c", "a.cpp"])
        self.assertEqual(args, ["a.cpp"])

    def test_drops_gcc_only_codegen_flags(self):
        args = dev._tidy_compile_arguments([
            "gcc",
            "-mlongcalls",
            "-mno-target-align",
            "-fstrict-volatile-bitfields",
            "-fno-tree-switch-conversion",
            "-fno-jump-tables",
            "-freorder-blocks",
            "-fno-test-coverage",
            "-std=gnu++26",
            "a.cpp",
        ])
        self.assertEqual(args, ["-std=gnu++26", "a.cpp"])

    def test_drops_prefix_flags(self):
        args = dev._tidy_compile_arguments([
            "gcc",
            "-specs=/opt/esp/idf/tools/esp32s3/specs",
            "-fmacro-prefix-map=/project/=",
            "-fdebug-prefix-map=/project/=/",
            "-ffile-prefix-map=/project/=/",
            "-fdiagnostics-color=always",
            "-DFOO=1",
            "a.cpp",
        ])
        self.assertEqual(args, ["-DFOO=1", "a.cpp"])

    def test_drops_value_taking_flags_with_their_value(self):
        args = dev._tidy_compile_arguments([
            "gcc",
            "-o",
            "a.cpp.obj",
            "-MF",
            "a.cpp.d",
            "-MT",
            "a.cpp.obj",
            "-MQ",
            "a.cpp.obj",
            "-DFOO=1",
            "a.cpp",
        ])
        self.assertEqual(args, ["-DFOO=1", "a.cpp"])

    def test_keeps_defines_and_include_paths(self):
        args = dev._tidy_compile_arguments([
            "gcc",
            "-DTHERMO_X=1",
            "-I/project/components/logic/include",
            "-isystem/opt/esp/idf/components/esp_common/include",
            "a.cpp",
        ])
        self.assertEqual(
            args,
            [
                "-DTHERMO_X=1",
                "-I/project/components/logic/include",
                "-isystem/opt/esp/idf/components/esp_common/include",
                "a.cpp",
            ],
        )


class TidyUnitSelectionTests(unittest.TestCase):
    """`_unique_tidy_units` picks one entry per C++ source in the repository."""

    ROOT = Path("/project")

    def _entry(self, path, directory="/project/mcgowan/build_esp32s3"):
        return {
            "directory": directory,
            "file": path,
            "arguments": ["gcc", "-c", path],
        }

    def _units(self, entries):
        return dev._unique_tidy_units(entries, root=self.ROOT)

    def test_keeps_cpp_sources_in_the_repository(self):
        entry = self._entry("/project/mcgowan/src/main.cpp")
        self.assertEqual(self._units([entry]), [entry])

    def test_skips_non_cpp_sources(self):
        entries = [
            self._entry("/project/mcgowan/src/main.c"),
            self._entry("/project/mcgowan/src/boot.S"),
        ]
        self.assertEqual(self._units(entries), [])

    def test_skips_sources_outside_the_repository(self):
        # ESP-IDF's own components and the generated project_elf_src file are
        # in the database but are not ours to analyze.
        entries = [
            self._entry("/opt/esp/idf/components/esp_common/src/foo.cpp"),
            self._entry(
                "/project/mcgowan/build_esp32s3/project_elf_src_esp32s3.c"
            ),
        ]
        self.assertEqual(self._units(entries), [])

    def test_duplicate_sources_are_analyzed_once(self):
        # A shared component is compiled by several subprojects.
        first = self._entry("/project/components/logic/src/payload.cpp")
        second = self._entry(
            "/project/components/logic/src/payload.cpp",
            directory="/project/test/build_linux",
        )
        self.assertEqual(self._units([first, second]), [first])

    def test_first_occurrence_wins(self):
        # Projects are seeded firmware-first, so the chip build's flags decide.
        firmware = self._entry("/project/components/logic/src/payload.cpp")
        host = self._entry(
            "/project/components/logic/src/payload.cpp",
            directory="/project/test/build_linux",
        )
        self.assertEqual(self._units([firmware, host])[0], firmware)
        self.assertEqual(self._units([host, firmware])[0], host)

    def test_entry_with_command_instead_of_arguments_is_supported(self):
        # The compilation database allows `command` instead of `arguments`.
        entry = {
            "directory": "/project/mcgowan/build_esp32s3",
            "command": "gcc -DFOO=1 -c /project/mcgowan/src/main.cpp",
            "file": "/project/mcgowan/src/main.cpp",
        }
        self.assertEqual(self._units([entry]), [entry])


class TidyConfigurationTests(unittest.TestCase):
    """clang-tidy configuration that the CI and commit gates depend on."""

    def test_projects_cover_every_buildable_subproject(self):
        # Every subproject must be analyzed; a missing one silently weakens
        # the gate.
        self.assertEqual(
            [project for project, _ in dev.TIDY_PROJECTS],
            list(dev.SUBPROJECTS),
        )

    def test_every_project_build_dir_is_the_one_dev_py_builds(self):
        # `clang-tidy` reads the databases `build` writes, so the paths must
        # agree or the gate would tell the user to build what is already built.
        for project, build_dir in dev.TIDY_PROJECTS:
            expected = (
                dev.FW_BUILD_DIR
                if project == dev.FIRMWARE_PROJECT
                else dev.TEST_BUILD_DIR
            )
            self.assertEqual(build_dir, expected, project)

    def test_header_filter_covers_our_sources_and_excludes_vendored_code(self):
        # The filter is anchored at the repository root, which is `/project`
        # inside the container and the checkout path on the host.
        root = str(dev.REPO_ROOT)
        pattern = re.compile(dev._tidy_header_filter())
        for path in (
            f"{root}/mcgowan/src/include/battery_monitor.hpp",
            f"{root}/components/logic/include/logic/payload.hpp",
            f"{root}/test/main/test_main.cpp",
            f"{root}/espidf-cpp-test/main/test_main.cpp",
            f"{root}/donny/main/console_task.hpp",
        ):
            self.assertIsNotNone(pattern.match(path), path)
        for path in (
            "/opt/esp/idf/components/esp_common/include/esp_err.h",
            "/opt/esp/tools/xtensa-esp-elf/esp-15.2.0/xtensa-esp-elf/"
            "include/c++/15.2.0/string",
            # Vendored code lives outside ROOTS and is not ours to fix.
            f"{root}/third_party/json/json.hpp",
            f"{root}/scripts/dev.py",
        ):
            self.assertIsNone(pattern.match(path), path)

    def test_command_is_registered(self):
        self.assertIn("clang-tidy", dev.COMMANDS)
        self.assertIs(dev.COMMANDS["clang-tidy"], dev.cmd_clang_tidy)


class TidyGateTests(unittest.TestCase):
    """`clang-tidy` is wired into the pre-commit hook and into CI."""

    def test_hook_runs_tidy_before_the_format_check(self):
        # Ordering matters: static analysis reports first, then formatting,
        # then the slow test suite.
        hook = (dev.REPO_ROOT / ".githooks" / "pre-commit").read_text()
        tidy = hook.index('run_check(dev, "clang-tidy")')
        fmt = hook.index('run_check(dev, "format-check-staged")')
        tests = hook.index('run_check(dev, "test")')
        self.assertLess(tidy, fmt)
        self.assertLess(fmt, tests)

    def test_ci_runs_tidy_after_the_build(self):
        # clang-tidy consumes the compile databases, so it must come after the
        # step that produces them.
        workflow = (
            dev.REPO_ROOT / ".github" / "workflows" / "ci.yml"
        ).read_text()
        build = workflow.index("python3 ./scripts/dev.py build")
        tidy = workflow.index("python3 ./scripts/dev.py clang-tidy")
        self.assertLess(build, tidy)


class StagedFormatCheckTests(unittest.TestCase):
    """`format-check-staged` must reproduce the style `format` would apply.

    clang-format discovers style by searching *upward* from each source file,
    so a nested `.clang-format` overrides the root one. `format` runs on the
    real working tree and honors that; `format-check-staged` runs on a
    materialized temp tree, so it has to recreate the same layout or the two
    disagree.

    They did disagree: only the root `.clang-format` was copied into the temp
    tree, so a nested `DisableFormat: true` (over vendored code) was ignored
    and the staged check rejected files that `format` leaves untouched. These
    tests pin the ancestor-search behavior that fixes it.
    """

    def _git(self, repo, *args):
        """Run git in `repo` (a scratch repository) and return stdout."""
        return subprocess.run(
            ("git", *args),
            cwd=repo,
            capture_output=True,
            text=True,
            check=True,
        ).stdout

    def _scratch_repo(self):
        """A throwaway git repository, so no test touches the real index."""
        tmp = tempfile.TemporaryDirectory()
        self.addCleanup(tmp.cleanup)
        repo = Path(tmp.name)
        self._git(repo, "init", "-q", ".")
        self._git(repo, "config", "user.email", "test@example.com")
        self._git(repo, "config", "user.name", "test")
        return repo

    def _write(self, repo, relative, content):
        """Write `content` to `repo/relative`, creating parent directories."""
        path = repo / relative
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(content)
        return path

    def _stage(self, repo, *relative):
        """Stage the named paths (paths are relative to `repo`)."""
        self._git(repo, "add", "--", *relative)

    def _commit(self, repo, message="snapshot"):
        """Commit the current index so later changes read as staged."""
        self._git(repo, "commit", "-q", "-m", message)

    # -- style discovery ----------------------------------------------------

    def test_style_files_are_found_by_ancestor_search(self):
        # Both the root style and the nested one apply to a nested source.
        repo = self._scratch_repo()
        self._write(repo, ".clang-format", "Root\n")
        self._write(repo, "vendor/.clang-format", "DisableFormat: true\n")
        self._write(repo, "vendor/lib/x.cpp", "int x;\n")
        self.assertEqual(
            dev._style_files_for(["vendor/lib/x.cpp"], repo),
            [".clang-format", "vendor/.clang-format"],
        )

    def test_style_discovery_only_walks_ancestors_of_the_given_files(self):
        # A style file in an unrelated tree cannot affect these files, so it
        # must not be materialized (it would just be dead weight).
        repo = self._scratch_repo()
        self._write(repo, ".clang-format", "Root\n")
        self._write(repo, "other/.clang-format", "Unrelated\n")
        self._write(repo, "mcgowan/main/x.cpp", "int x;\n")
        self.assertEqual(
            dev._style_files_for(["mcgowan/main/x.cpp"], repo),
            [".clang-format"],
        )

    def test_style_discovery_includes_the_clang_format_ignore_file(self):
        repo = self._scratch_repo()
        self._write(repo, ".clang-format", "Root\n")
        self._write(repo, "vendor/.clang-format-ignore", "*.c\n")
        self._write(repo, "vendor/x.cpp", "int x;\n")
        self.assertEqual(
            dev._style_files_for(["vendor/x.cpp"], repo),
            [".clang-format", "vendor/.clang-format-ignore"],
        )

    def test_style_discovery_finds_the_real_root_style(self):
        # Against the actual repository, a real source file resolves to the
        # committed root style (which must stay tracked for a fresh clone).
        self.assertEqual(
            dev._style_files_for(
                ["components/logic/include/logic/led_task.hpp"]
            ),
            [".clang-format"],
        )

    # -- materialization ----------------------------------------------------

    def test_materialize_copies_a_nested_style_to_its_relative_path(self):
        # The nested style must land at the same depth in the temp tree, or
        # clang-format's upward search will not reach it.
        repo = self._scratch_repo()
        self._write(repo, ".clang-format", "Root\n")
        self._write(repo, "vendor/.clang-format", "DisableFormat: true\n")
        self._write(repo, "vendor/x.cpp", "int   x;\n")
        self._stage(repo, ".clang-format", "vendor/.clang-format")
        self._stage(repo, "vendor/x.cpp")

        with tempfile.TemporaryDirectory() as tmp:
            tmp_dir = Path(tmp)
            dev._materialize_staged(["vendor/x.cpp"], tmp_dir, repo)
            self.assertEqual(
                (tmp_dir / ".clang-format").read_text(), "Root\n"
            )
            self.assertEqual(
                (tmp_dir / "vendor" / ".clang-format").read_text(),
                "DisableFormat: true\n",
            )
            self.assertEqual(
                (tmp_dir / "vendor" / "x.cpp").read_text(), "int   x;\n"
            )

    def test_materialize_uses_the_staged_content_not_the_working_copy(self):
        # The check must validate what is about to be committed, so an
        # unstaged edit in the working tree is irrelevant.
        repo = self._scratch_repo()
        self._write(repo, ".clang-format", "Root\n")
        self._write(repo, "x.cpp", "int staged;\n")
        self._stage(repo, ".clang-format", "x.cpp")
        self._write(repo, "x.cpp", "int working_tree;\n")

        with tempfile.TemporaryDirectory() as tmp:
            tmp_dir = Path(tmp)
            dev._materialize_staged(["x.cpp"], tmp_dir, repo)
            self.assertEqual(
                (tmp_dir / "x.cpp").read_text(), "int staged;\n"
            )

    def test_materialize_skips_a_style_file_staged_for_deletion(self):
        # A style file being removed is not part of the commit, so the temp
        # tree must not contain it (otherwise the check would use a style the
        # commit no longer has). `git rm --cached` keeps the working copy,
        # which is exactly the case the ancestor search still finds.
        repo = self._scratch_repo()
        self._write(repo, ".clang-format", "Root\n")
        self._write(repo, "vendor/.clang-format", "DisableFormat: true\n")
        self._write(repo, "x.cpp", "int   x;\n")
        self._stage(repo, ".clang-format", "vendor/.clang-format", "x.cpp")
        self._commit(repo)
        self._git(repo, "rm", "-q", "--cached", "--", "vendor/.clang-format")
        self._write(repo, "x.cpp", "int   changed;\n")
        self._stage(repo, "x.cpp")

        with tempfile.TemporaryDirectory() as tmp:
            tmp_dir = Path(tmp)
            dev._materialize_staged(["x.cpp"], tmp_dir, repo)
            self.assertTrue((tmp_dir / ".clang-format").is_file())
            self.assertFalse((tmp_dir / "vendor" / ".clang-format").exists())

    def test_materialize_includes_an_untracked_style_file(self):
        # `format` honors a style file in the working tree whether or not it is
        # tracked; the staged check must agree rather than silently fall back
        # to the root style.
        repo = self._scratch_repo()
        self._write(repo, ".clang-format", "Root\n")
        self._write(repo, "vendor/.clang-format", "DisableFormat: true\n")
        self._write(repo, "vendor/x.cpp", "int   x;\n")
        self._stage(repo, ".clang-format", "vendor/x.cpp")

        with tempfile.TemporaryDirectory() as tmp:
            tmp_dir = Path(tmp)
            dev._materialize_staged(["vendor/x.cpp"], tmp_dir, repo)
            self.assertEqual(
                (tmp_dir / "vendor" / ".clang-format").read_text(),
                "DisableFormat: true\n",
            )

    # -- end to end, with a real clang-format ------------------------------

    def _format_check_in_tree(self, repo, files):
        """Materialize `files` from `repo` and run the real staged check."""
        with tempfile.TemporaryDirectory() as tmp:
            tmp_dir = Path(tmp)
            dev._materialize_staged(files, tmp_dir, repo)
            return dev._run_staged_format_check(tmp_dir, files)

    @unittest.skipUnless(
        shutil.which("clang-format"), "clang-format is not on PATH"
    )
    def test_nested_disable_format_style_lets_unformatted_code_pass(self):
        # The regression: the nested `DisableFormat: true` used to be dropped,
        # so this unformatted (but deliberately unformatted) file was rejected.
        repo = self._scratch_repo()
        self._write(repo, ".clang-format", "BasedOnStyle: Google\n")
        self._write(repo, "vendor/.clang-format", "DisableFormat: true\n")
        self._write(repo, "vendor/x.cpp", "int   x(int   y){return y;}\n")
        self._stage(repo, ".clang-format", "vendor/.clang-format")
        self._stage(repo, "vendor/x.cpp")
        self.assertEqual(self._format_check_in_tree(repo, ["vendor/x.cpp"]), 0)

    @unittest.skipUnless(
        shutil.which("clang-format"), "clang-format is not on PATH"
    )
    def test_root_style_still_rejects_unformatted_code(self):
        # The fix must not blunt the check: with no nested exemption, the very
        # same code is still rejected.
        repo = self._scratch_repo()
        self._write(repo, ".clang-format", "BasedOnStyle: Google\n")
        self._write(repo, "vendor/x.cpp", "int   x(int   y){return y;}\n")
        self._stage(repo, ".clang-format", "vendor/x.cpp")
        self.assertNotEqual(
            self._format_check_in_tree(repo, ["vendor/x.cpp"]), 0
        )


class SourceFileDiscoveryTests(unittest.TestCase):
    """`source_files` must see untracked sources, not just the index.

    `format` used to enumerate with plain `git ls-files`, whose default is the
    index. A brand-new source that had not been `git add`ed was therefore
    silently skipped: `format` left it untouched, and the pre-commit hook's
    `format-check-staged` (which reads the staged content by definition) then
    rejected the commit with a wall of violations. The fix enumerates
    `--cached --others --exclude-standard`, so `format` covers the working tree
    while still ignoring build output.
    """

    def _git(self, repo, *args):
        """Run git in `repo` (a scratch repository) and return stdout."""
        return subprocess.run(
            ("git", *args),
            cwd=repo,
            capture_output=True,
            text=True,
            check=True,
        ).stdout

    def _scratch_repo(self):
        """A throwaway git repository, so no test touches the real index."""
        tmp = tempfile.TemporaryDirectory()
        self.addCleanup(tmp.cleanup)
        repo = Path(tmp.name)
        self._git(repo, "init", "-q", ".")
        self._git(repo, "config", "user.email", "test@example.com")
        self._git(repo, "config", "user.name", "test")
        return repo

    def _write(self, repo, relative, content):
        """Write `content` to `repo/relative`, creating parent directories."""
        path = repo / relative
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(content)
        return path

    def _listed(self, repo):
        """The paths `_source_file_command` reports for a scratch repository."""
        # `_source_file_command` starts with "git"; `_git` adds that itself.
        argv = dev._source_file_command(roots=(".",))[1:]
        return self._git(repo, *argv).splitlines()

    # -- the command itself -------------------------------------------------

    def test_command_lists_cached_and_untracked_with_standard_excludes(self):
        # The three flags that matter: tracked files, untracked files, and the
        # `.gitignore` filter that keeps build output out.
        argv = dev._source_file_command()
        self.assertEqual(argv[:2], ["git", "ls-files"])
        self.assertIn("--cached", argv)
        self.assertIn("--others", argv)
        self.assertIn("--exclude-standard", argv)

    def test_command_is_scoped_to_roots(self):
        # The pathspec keeps `third_party/` (vendored, must stay
        # byte-identical) out of the walk.
        argv = dev._source_file_command()
        separator = argv.index("--")
        self.assertEqual(tuple(argv[separator + 1:]), dev.ROOTS)

    # -- selection ----------------------------------------------------------

    def test_selection_keeps_only_cpp_extensions(self):
        repo = self._scratch_repo()
        for name in ("a.cpp", "b.hpp", "c.h", "d.py", "e.txt"):
            self._write(repo, name, "")
        self.assertEqual(
            dev._select_sources(
                ["a.cpp", "b.hpp", "c.h", "d.py", "e.txt"], repo
            ),
            ["a.cpp", "b.hpp", "c.h"],
        )

    def test_selection_drops_paths_that_are_not_files(self):
        # A tracked-but-deleted file is still in the index; handing it to
        # clang-format would fail the run.
        repo = self._scratch_repo()
        self._write(repo, "kept.cpp", "")
        self.assertEqual(
            dev._select_sources(["kept.cpp", "gone.cpp"], repo), ["kept.cpp"]
        )

    def test_selection_is_sorted_and_deduplicated(self):
        repo = self._scratch_repo()
        self._write(repo, "b.cpp", "")
        self._write(repo, "a.cpp", "")
        self.assertEqual(
            dev._select_sources(["b.cpp", "a.cpp", "a.cpp"], repo),
            ["a.cpp", "b.cpp"],
        )

    # -- end to end, against a scratch repository ---------------------------

    def test_untracked_source_is_included(self):
        # The regression: a source that exists but is not in the index must
        # still be handed to clang-format.
        repo = self._scratch_repo()
        self._write(repo, "src/new.cpp", "int x;\n")
        self.assertTrue(
            any(path.endswith("src/new.cpp") for path in self._listed(repo)),
            self._listed(repo),
        )

    def test_ignored_source_is_excluded(self):
        # `--exclude-standard` must keep build trees out of the walk even
        # though they are untracked.
        repo = self._scratch_repo()
        self._write(repo, ".gitignore", "build/\n")
        self._write(repo, "build/generated.cpp", "int x;\n")
        self._write(repo, "src/real.cpp", "int x;\n")
        listed = self._listed(repo)
        self.assertTrue(any(p.endswith("src/real.cpp") for p in listed), listed)
        self.assertFalse(
            any("generated.cpp" in p for p in listed), listed
        )

    def test_tracked_and_untracked_sources_are_both_listed(self):
        # `--cached` alone lists only the first and `--others` only the
        # second, so both flags are required.
        repo = self._scratch_repo()
        self._write(repo, "src/tracked.cpp", "int x;\n")
        self._git(repo, "add", "--", "src/tracked.cpp")
        self._write(repo, "src/untracked.cpp", "int x;\n")
        listed = self._listed(repo)
        self.assertTrue(
            any(p.endswith("src/tracked.cpp") for p in listed), listed
        )
        self.assertTrue(
            any(p.endswith("src/untracked.cpp") for p in listed), listed
        )


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
