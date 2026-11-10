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
            dev._resolve_container_directory("components"),
            "/project/components",
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
            (
                "mcgowan",
                "test-hal-stub",
                "test-hal-common",
                "test",
                "espidf-cpp-test",
                "donny",
            ),
        )

    def test_linux_projects_are_the_host_target_subprojects(self):
        self.assertEqual(
            dev.LINUX_PROJECTS,
            (
                dev.HAL_STUB_TEST_PROJECT,
                dev.HAL_COMMON_TEST_PROJECT,
                dev.TEST_PROJECT,
                dev.ESPIDF_CPP_TEST_PROJECT,
                dev.DONNY_PROJECT,
            ),
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
                    "-DSUBPROJECTS=mcgowan;test-hal-stub;test-hal-common;"
                    "test;espidf-cpp-test;donny",
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
                self.assertIn(
                    "_ensure_submodules()", self._function_body(name)
                )

    def test_submodules_command_does_not_hop_into_a_container(self):
        # `git submodule update` runs on whichever side invoked dev.py, so the
        # vendored sources land in the real working tree either way.
        self.assertIn(
            "change_to_container()", self._function_body("cmd_submodules")
        )

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
        self.assertFalse(
            dev._is_vendored(Path("components/sml/CMakeLists.txt"))
        )
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
        # `add_custom_target(${sub}_build ALL ...)` is the one target
        # statement.
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
                project_name = configuration.get("PROJECT_NAME")
                self.assertIn(
                    configuration.get("TARGET_NAME"),
                    root,
                    f"{name}: TARGET_NAME "
                    f"{configuration.get('TARGET_NAME')!r} is not a target of "
                    f"{project_name!r} (has {sorted(root)})",
                )


class DevImageTests(unittest.TestCase):
    """The pinned prebuilt image is wired up consistently.

    The dev image is pulled, not built: `docker-compose.yml`, `dev.py`
    (`DEV_IMAGE`) and CI must all name the same tag, or CI would run a
    different toolchain than a developer does.
    """

    def test_dev_image_is_the_pinned_prebuilt_tag(self):
        self.assertEqual(dev.DEV_IMAGE, "marcpawl/esp-idf:20261109b")

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
        # one that CI and every developer already reference. A second publish
        # on the same day takes a letter suffix (20261109 -> 20261109b); it is
        # a new tag, never a moved one.
        repository, _, tag = dev.DEV_IMAGE.partition(":")
        self.assertEqual(repository, "marcpawl/esp-idf")
        self.assertRegex(tag, r"^\d{8}[a-z]?$")

    def test_dockerfile_extends_the_official_espressif_image(self):
        # The pinned image is the official ESP-IDF v6.1 image plus project
        # tooling; AGENTS.md's toolchain note relies on this.
        dockerfile = (dev.REPO_ROOT / "Dockerfile").read_text()
        self.assertIn("FROM espressif/idf:release-v6.1", dockerfile)

    def test_spec_names_the_pinned_image(self):
        # The spec's environment table is the first thing a new contributor
        # reads, so it must not contradict the pinned image. (The spec keeps no
        # change history of its own; git log is the record.)
        spec = (dev.REPO_ROOT / "Specification.md").read_text()
        self.assertIn(dev.DEV_IMAGE, spec)

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
        self.assertIn(
            "idf.py --version", self._devcontainer()["postCreateCommand"]
        )

    def test_uses_the_compose_dev_service(self):
        # The Dev Container must not define its own image: it inherits the
        # pinned one from docker-compose.yml.
        text = self._devcontainer()
        self.assertEqual(text["dockerComposeFile"], "../docker-compose.yml")
        self.assertEqual(text["service"], "dev")
        self.assertNotIn("image", text)
        self.assertNotIn("build", text)

    def test_inherits_the_host_home_mounts_from_compose(self):
        # The Dev Container reuses the compose service, so `~/.continue`,
        # `~/.ssh`, and `~/.config/gh` are inherited. A `mounts` entry here
        # would layer another mount over the compose one, shadowing its source,
        # so all of them must stay in docker-compose.yml alone.
        text = (
            dev.REPO_ROOT / ".devcontainer" / "devcontainer.json"
        ).read_text()
        self.assertNotIn('"mounts"', text)


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
            dev.run = (
                lambda cmd, cwd=dev.REPO_ROOT:
                calls.append(list(cmd)) or 0
            )
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
            dev.run = (
                lambda cmd, cwd=dev.REPO_ROOT:
                calls.append(list(cmd)) or 0
            )
            dev._container_environment = lambda user: ["-e", "HOME=/tmp"]
            dev.compose_run(["dev.py", "build"])
        finally:
            dev.run, dev._container_environment = original_run, original_env
        command = calls[0]
        # Everything after the service name belongs to the command.
        self.assertLess(command.index("-e"), command.index("dev"))
        self.assertLess(command.index("--rm"), command.index("dev"))
        self.assertEqual(
            command[command.index("dev") + 1:], ["dev.py", "build"]
        )


def _compose_home_bind_mounts() -> dict[str, tuple[str, str]]:
    """The host `$HOME` bind mounts in docker-compose.yml, keyed by target.

    Parses the `- ${HOME:?...}/<dir>:<target>[:<mode>]` entries of the `dev`
    service and returns `{target: (source, mode)}`, where `mode` is "ro" for a
    read-only mount and "rw" otherwise.

    The source starts with a `${HOME:?...}` expansion, which contains a colon
    of its own, so splitting has to skip past the closing brace first. Reading
    the file (rather than running `docker compose config`) keeps this
    dependency-free, so `self-check` needs no Docker.
    """
    compose = (dev.REPO_ROOT / "docker-compose.yml").read_text()
    mounts = {}
    for line in compose.splitlines():
        entry = line.strip().removeprefix("- ").strip()
        if entry.startswith("#") or not entry.startswith("${HOME:"):
            continue
        brace = entry.index("}")
        suffix, _, rest = entry[brace + 1:].partition(":")
        source = entry[: brace + 1] + suffix
        target, _, mode = rest.partition(":")
        mounts[target] = (source, mode or "rw")
    return mounts


def _compose_dev_environment() -> dict[str, str | None]:
    """The `dev` service's `environment:` mapping in docker-compose.yml.

    Returns `{name: value}`, where `None` means the entry was written with no
    value at all. That is compose's spelling of "take it from the host
    environment": an unset host variable stays unset instead of being passed
    as an empty string (which tools like `gh` would read as a bad credential).

    Parsed line by line, like `_compose_home_bind_mounts`, so `self-check`
    needs no Docker and no YAML library. The block ends at the first line
    indented no deeper than the `environment:` key itself.
    """
    compose = (dev.REPO_ROOT / "docker-compose.yml").read_text()
    lines = compose.splitlines()
    for index, line in enumerate(lines):
        stripped = line.strip()
        if stripped != "environment:":
            continue
        depth = len(line) - len(line.lstrip())
        environment: dict[str, str | None] = {}
        for entry in lines[index + 1:]:
            if not entry.strip() or entry.strip().startswith("#"):
                continue
            if len(entry) - len(entry.lstrip()) <= depth:
                return environment
            name, _, value = entry.strip().partition(":")
            environment[name] = value.strip() or None
        return environment
    raise AssertionError("docker-compose.yml has no `environment:` block")


class ContinueCliMountTests(unittest.TestCase):
    """The host's `~/.continue` is shared with the container.

    `cn` keeps its config, models, and sessions under `$HOME/.continue`. Users
    expect `ls ~/.continue` to list the same files on the host and inside the
    container (`./scripts/dev.py shell ls '~/.continue'`), which needs a bind
    mount. A Dockerfile cannot mount host paths, so the mount lives in
    docker-compose.yml — which the Dev Container reuses, so the IDE gets it
    too.

    The container's HOME depends on the uid it runs as (see
    ContainerUserTests), so a single mount point cannot cover every user.
    """

    TARGET_UID_HOME = "/home/dev"
    OTHER_UID_HOME = "/tmp"

    def _mounts(self) -> dict[str, tuple[str, str]]:
        """The `~/.continue` bind mounts, keyed by target."""
        return {
            target: mount
            for target, mount in _compose_home_bind_mounts().items()
            if target.endswith("/.continue")
        }

    def test_host_continue_dir_is_mounted_at_the_image_users_home(self):
        self.assertEqual(
            self._mounts().get(f"{self.TARGET_UID_HOME}/.continue"),
            (
                "${HOME:?HOME must be set to share the Continue CLI config}"
                "/.continue",
                "rw",
            ),
        )

    def test_host_continue_dir_is_mounted_where_other_uids_home_points(self):
        # The coupling that matters: `dev.py` pins HOME for any uid other than
        # the image's 1000, so the mount must be made at *that* path too. If
        # HOME ever changes, this mount silently stops applying.
        flags = dev._container_environment("1001:1001")
        [home_flag] = [flag for flag in flags if flag.startswith("HOME=")]
        other_home = home_flag.split("=", 1)[1]
        self.assertEqual(other_home, self.OTHER_UID_HOME)
        self.assertIn(f"{other_home}/.continue", self._mounts())

    def test_mount_refuses_to_fall_back_to_the_filesystem_root(self):
        # `${HOME}` unset would make the source `/.continue`. The `:?` form
        # fails the run loudly instead.
        mounts = self._mounts()
        # Guard against passing vacuously if the mount lines are ever renamed.
        self.assertEqual(len(mounts), 2, mounts)
        for source, _ in mounts.values():
            self.assertIn("${HOME:?", source)

    def test_image_creates_both_mount_points(self):
        # A bind mount needs an existing destination with the right owner;
        # Docker would otherwise create it as root. Both paths are created, and
        # /tmp/.continue is world-writable like /tmp so it works without the
        # mount too.
        dockerfile = (dev.REPO_ROOT / "Dockerfile").read_text()
        self.assertIn("mkdir -p /tmp/.continue", dockerfile)
        self.assertIn('"/home/${USERNAME}/.continue"', dockerfile)
        self.assertIn("chmod 1777 /tmp/.continue", dockerfile)

    def test_image_installs_the_continue_cli_and_its_node_runtime(self):
        # `cn` needs Node >= 20.20.1 and the base image ships no Node at all.
        dockerfile = (dev.REPO_ROOT / "Dockerfile").read_text()
        self.assertIn("@continuedev/cli@${CONTINUE_CLI_VERSION}", dockerfile)
        self.assertIn("ARG NODE_VERSION=", dockerfile)
        # The tarball is pinned by checksum, not just by URL.
        self.assertIn("sha256sum --check --strict", dockerfile)
        # Installed globally (/usr/local) so every uid finds it on PATH.
        self.assertIn("npm install --global", dockerfile)


class ContinueCliCommandTests(unittest.TestCase):
    """`dev.py cn` runs the Continue CLI with the host's mounted config.

    `cn` is installed in the image, and its config is the host's
    `~/.continue/config.yaml` (bind-mounted by docker-compose.yml). That config
    is named explicitly with `--config`: cn's own default lookup is not
    reliable (it can fall back to the remote default config), and only this
    file declares the DeepSeek models and the `secrets.DEEPSEEK_API_KEY` they
    authenticate with.
    """

    # The two homes `dev.py` can give the container (see `_container_home`).
    IMAGE_USER = "1000:1000"
    OTHER_USER = "1001:1001"

    def _config_flag(self, argv: list[str]) -> str:
        """The value of `--config` in a `cn` argv."""
        self.assertIn("--config", argv, argv)
        return argv[argv.index("--config") + 1]

    def test_command_is_registered(self):
        self.assertIn("cn", dev.COMMANDS)
        self.assertIs(dev.COMMANDS["cn"], dev.cmd_cn)
        self.assertTrue((dev.cmd_cn.__doc__ or "").strip())

    def test_config_is_the_mounted_continue_config_of_the_image_user(self):
        argv = dev._cn_command([], home=dev._container_home(self.IMAGE_USER))
        self.assertEqual(argv[:2], ["cn", "--config"])
        self.assertEqual(
            self._config_flag(argv), "/home/dev/.continue/config.yaml"
        )

    def test_config_follows_the_home_dev_py_pins_for_other_uids(self):
        # A uid other than the image's 1000 gets HOME=/tmp, and compose mounts
        # the host's `~/.continue` there too. The same helper must agree, or
        # `--config` would name a path that is not the host's config.
        home = dev._container_home(self.OTHER_USER)
        self.assertEqual(home, "/tmp")
        self.assertEqual(
            self._config_flag(dev._cn_command([], home=home)),
            "/tmp/.continue/config.yaml",
        )

    def test_config_path_matches_a_compose_mount_target(self):
        # `--config` names a file under the container's HOME; that directory is
        # the host's `~/.continue` only because compose mounts it there.
        mounts = _compose_home_bind_mounts()
        for user in (self.IMAGE_USER, self.OTHER_USER):
            with self.subTest(user=user):
                home = dev._container_home(user)
                self.assertIn(f"{home}/.continue", mounts)

    def test_extra_arguments_are_passed_through_verbatim(self):
        # `--config` comes first and the caller's arguments follow unchanged,
        # so every `cn` flag works (the last `--config` would still win).
        argv = dev._cn_command(
            ["--format", "json", "-p", "list the files"], home="/home/dev"
        )
        self.assertEqual(
            argv,
            [
                "cn",
                "--config", "/home/dev/.continue/config.yaml",
                "--format", "json", "-p", "list the files",
            ],
        )

    def test_command_runs_in_the_container(self):
        # `cn` only exists inside the dev image.
        source = (dev.REPO_ROOT / "scripts" / "dev.py").read_text()
        body = source.split("def cmd_cn", 1)[1].split("\ndef ", 1)[0]
        self.assertIn("change_to_container()", body)

    def test_main_forwards_cn_arguments_without_parsing_them(self):
        # Every cn argument is a flag argparse would claim (`--help`) or reject
        # (`-p`), so `main` must hand them over untouched.
        calls = []
        original = dev.cmd_cn
        try:
            dev.cmd_cn = lambda extra: calls.append(extra) or 0
            rc = dev.main(
                [str(dev.REPO_ROOT / "scripts" / "dev.py"),
                 "cn", "-p", "hello world"]
            )
        finally:
            dev.cmd_cn = original
        self.assertEqual(rc, 0)
        self.assertEqual(calls, [["-p", "hello world"]])

    def test_main_rewrites_the_script_path_before_re_executing(self):
        # `cmd_cn` -> `change_to_container` re-execs `sys.argv` inside the
        # container, so `sys.argv[0]` must already be repo-relative when `cn`
        # is dispatched: an absolute host path (e.g. when dev.py is invoked by
        # absolute path from another directory) does not exist there. This
        # pins the dispatch below the rewrite, not merely somewhere in `main`.
        seen = []
        original = dev.cmd_cn
        original_argv = sys.argv
        try:
            dev.cmd_cn = lambda extra: seen.append(sys.argv[0]) or 0
            sys.argv = [str(dev.REPO_ROOT / "scripts" / "dev.py"), "cn"]
            rc = dev.main(sys.argv)
        finally:
            dev.cmd_cn = original
            sys.argv = original_argv
        self.assertEqual(rc, 0)
        self.assertEqual(seen, ["scripts/dev.py"])

    def test_cn_is_listed_in_help(self):
        # It is registered in COMMANDS, so it shows up in `--help` even though
        # its arguments are not parsed by argparse.
        buf = io.StringIO()
        with redirect_stdout(buf), self.assertRaises(SystemExit):
            dev._build_parser().parse_args(["--help"])
        listing = " ".join(buf.getvalue().split())
        self.assertIn("cn ", listing)

    def test_deepseek_key_is_passed_through_from_the_host_environment(self):
        # The config's `${{ secrets.DEEPSEEK_API_KEY }}` is resolved from the
        # environment, so the key is passed through like GH_TOKEN: a null value
        # means "take it from the host env" and is never written here.
        service = _compose_dev_environment()
        self.assertIn("DEEPSEEK_API_KEY", service, service)
        self.assertIsNone(service["DEEPSEEK_API_KEY"])
        compose = (dev.REPO_ROOT / "docker-compose.yml").read_text()
        for line in compose.splitlines():
            stripped = line.strip()
            if stripped.startswith("DEEPSEEK_API_KEY:"):
                self.assertEqual(stripped, "DEEPSEEK_API_KEY:")


class SshMountTests(unittest.TestCase):
    """The host's `~/.ssh` is shared with the container, read-only.

    `git push` over SSH needs the host's private key and a matching
    `known_hosts`. Mounting the directory (rather than copying keys into the
    image) means the key never leaves the host filesystem and there is nothing
    per-developer to keep out of the image.

    Read-only is deliberate: the container can authenticate but cannot modify
    or delete the host's private keys, nor rewrite `known_hosts`. The cost is
    that a host key missing from the host's `known_hosts` cannot be added from
    inside the container.

    Like `~/.continue`, the target is duplicated because the container's HOME
    is uid-dependent (see ContainerUserTests).
    """

    def _mounts(self) -> dict[str, tuple[str, str]]:
        """The `~/.ssh` bind mounts, keyed by target."""
        return {
            target: mount
            for target, mount in _compose_home_bind_mounts().items()
            if target.endswith("/.ssh")
        }

    def test_host_ssh_dir_is_mounted_at_both_possible_homes(self):
        # Both the image user's home and the HOME `dev.py` pins for any other
        # uid, so `~/.ssh` resolves to the host directory either way.
        self.assertIn("/home/dev/.ssh", self._mounts())
        self.assertIn("/tmp/.ssh", self._mounts())

    def test_ssh_mount_is_read_only(self):
        # The private key must be usable but not mutable from the container.
        modes = {mode for _, mode in self._mounts().values()}
        self.assertEqual(modes, {"ro"}, self._mounts())

    def test_ssh_mount_comes_from_the_host_home(self):
        # Not a named volume and not baked into the image: every source must be
        # the host's `$HOME/.ssh`, with `:?` so an unset HOME fails loudly.
        mounts = self._mounts()
        self.assertEqual(len(mounts), 2, mounts)
        for source, _ in mounts.values():
            self.assertTrue(source.startswith("${HOME:?"), source)
            self.assertTrue(source.endswith("/.ssh"), source)

    def test_image_creates_the_ssh_mount_points(self):
        # A bind mount needs a destination. Both are created 0700 and owned by
        # the container user, which is what ssh requires before it will use a
        # key (the read-only host mount carries the host's own modes).
        dockerfile = (dev.REPO_ROOT / "Dockerfile").read_text()
        self.assertIn('"/home/${USERNAME}/.ssh"', dockerfile)
        self.assertIn("install -d -m 700", dockerfile)
        self.assertIn("/tmp/.ssh", dockerfile)


class GhCliMountTests(unittest.TestCase):
    """The host's `~/.config/gh` is shared with the container, read/write.

    `gh` is an API client, so the host's `~/.ssh` does not log it in: its
    commands (pr, release, api, issue, ...) call the GitHub REST API with an
    OAuth token or a PAT, which `gh` reads from `$HOME/.config/gh` or from
    `GH_TOKEN` / `GITHUB_TOKEN`.

    Two independent paths are provided on purpose:

    * the environment passthrough below is the recommended one — `GH_TOKEN`
      takes precedence over any stored credential, and it works even when the
      host keeps its token in a desktop keyring the container cannot reach;
    * the bind mount covers the case where `gh auth login` stored the token in
      `hosts.yml`, and it needs no token in the environment at all.

    Read/write (unlike `~/.ssh`) so that `gh auth login` inside the container
    persists to the host, which is what makes the two logins one.
    """

    def _mounts(self) -> dict[str, tuple[str, str]]:
        """The `~/.config/gh` bind mounts, keyed by target."""
        return {
            target: mount
            for target, mount in _compose_home_bind_mounts().items()
            if target.endswith("/.config/gh")
        }

    def test_host_gh_config_is_mounted_at_both_possible_homes(self):
        # Both the image user's home and the HOME `dev.py` pins for any other
        # uid, so `gh` finds its state whichever uid the container runs as.
        self.assertIn("/home/dev/.config/gh", self._mounts())
        self.assertIn("/tmp/.config/gh", self._mounts())

    def test_gh_mount_is_read_write(self):
        # `gh auth login` in the container must persist back to the host, so
        # unlike `~/.ssh` this mount cannot be read-only.
        modes = {mode for _, mode in self._mounts().values()}
        self.assertEqual(modes, {"rw"}, self._mounts())

    def test_gh_mount_comes_from_the_host_home(self):
        # Not a named volume and not baked into the image. `${HOME:?...}`
        # fails the run loudly rather than mounting `/.config/gh` when the host
        # HOME is unset.
        mounts = self._mounts()
        self.assertEqual(len(mounts), 2, mounts)
        for source, _ in mounts.values():
            self.assertTrue(source.startswith("${HOME:?"), source)
            self.assertTrue(source.endswith("/.config/gh"), source)

    def test_image_creates_the_gh_mount_points(self):
        # A bind mount needs a destination owned by the container user, or
        # Docker creates it as root and `gh` cannot write to it without the
        # mount.
        dockerfile = (dev.REPO_ROOT / "Dockerfile").read_text()
        self.assertIn('"/home/${USERNAME}/.config/gh"', dockerfile)
        self.assertIn("/tmp/.config/gh", dockerfile)

    def test_other_uids_home_is_world_writable(self):
        # `dev.py` gives every uid but the image's 1000 a HOME of /tmp, and
        # this mount is skipped whenever the host has no `gh` login. A 0700
        # /tmp/.config/gh owned by uid 1000 makes gh die with
        # "failed to read configuration: ... permission denied" for those
        # uids, instead of the intended "You are not logged into any GitHub
        # hosts" (which is what a bare `docker run -u 1234` showed).
        dockerfile = (dev.REPO_ROOT / "Dockerfile").read_text()
        install = dockerfile.index("install -d -m 700")
        [chmod_line] = [
            line
            for line in dockerfile[:install].splitlines()
            if line.strip().startswith("&& chmod 1777")
        ]
        self.assertIn("/tmp/.config/gh", chmod_line)
        self.assertIn("/tmp/.continue", chmod_line)

    def test_token_is_passed_through_from_the_host_environment(self):
        # The recommended path. A null value in the compose `environment:`
        # mapping means "take it from the host env", so the token is never
        # written into the committed file and an unset variable stays unset
        # (rather than becoming an empty string gh would read as a credential).
        service = _compose_dev_environment()
        for name in ("GH_TOKEN", "GITHUB_TOKEN"):
            self.assertIn(name, service, service)
            self.assertIsNone(service[name], f"{name} must not be hardcoded")


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

    def test_host_test_projects_cover_every_suite(self):
        projects = [project for project, _ in dev.HOST_TEST_PROJECTS]
        self.assertEqual(
            projects,
            [
                dev.HAL_COMMON_TEST_PROJECT,
                dev.HAL_STUB_TEST_PROJECT,
                dev.TEST_PROJECT,
                dev.ESPIDF_CPP_TEST_PROJECT,
            ],
        )
        # `test` runs `hal_common` first: it covers the shared vocabulary the
        # other suites build on, and `hal_stub` follows it.
        self.assertEqual(projects[0], dev.HAL_COMMON_TEST_PROJECT)
        self.assertEqual(projects[1], dev.HAL_STUB_TEST_PROJECT)
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


class HostTestCommandTests(unittest.TestCase):
    """The `test*` commands run the suites they name.

    There are four host test suites (the `hal_common` vocabulary tests, the
    `hal_stub` device tests, the `test/` application tests, and the
    `espidf_cpp` wrapper tests). `test` is the aggregate the pre-commit hook
    and CI call, so it must run all four, with `test-hal-common` first and
    `test-hal-stub` next; each suite also has a command of its own so one can
    be run alone while working on it.
    """

    def setUp(self):
        # Record the commands `run()` would execute instead of executing them.
        self.calls = []
        self._original_run = dev.run
        self._original_change_to_container = dev.change_to_container
        self.hops = []

        def fake_run(cmd, cwd=dev.REPO_ROOT):
            self.calls.append(list(cmd))
            return 0

        def fake_change_to_container():
            # The real one re-execs `sys.argv` under `docker compose` and then
            # `sys.exit()`s, so it would never return here. Record the hop
            # instead and let the command under test run its own body: that is
            # what makes the suite exercise the command's decisions on the
            # host (where `self-check` is allowed to run) rather than its
            # container handoff.
            self.hops.append(True)

        dev.run = fake_run
        dev.change_to_container = fake_change_to_container

    def tearDown(self):
        dev.run = self._original_run
        dev.change_to_container = self._original_change_to_container

    def test_every_suite_command_is_registered(self):
        for name, handler in (
            ("test-hal-stub", dev.cmd_test_hal_stub),
            ("test-hal-common", dev.cmd_test_hal_common),
            ("test-linux", dev.cmd_test_linux),
            ("test-espidf-cpp", dev.cmd_test_espidf_cpp),
        ):
            with self.subTest(command=name):
                self.assertIn(name, dev.COMMANDS)
                self.assertIs(dev.COMMANDS[name], handler)

    def test_every_suite_command_hops_into_the_container(self):
        # `idf.py` only exists inside the dev image, so each command must hand
        # off to it exactly once (`_run_host_tests` itself never hops).
        for handler in (
            dev.cmd_test_hal_stub,
            dev.cmd_test_hal_common,
            dev.cmd_test_linux,
            dev.cmd_test_espidf_cpp,
            dev.cmd_test,
        ):
            with self.subTest(command=handler.__name__):
                self.hops = []
                handler()
                self.assertEqual(len(self.hops), 1)

    def test_test_espidf_cpp_uses_the_build_commands_project(self):
        # `build-espidf-cpp-test` builds the wrapper suite and
        # `test-espidf-cpp` builds and runs it: both must resolve to the same
        # project, or the two commands would drift apart.
        self.assertEqual(
            dev._host_test_projects((dev.ESPIDF_CPP_TEST_PROJECT,)),
            [
                (
                    dev.ESPIDF_CPP_TEST_PROJECT,
                    dict(dev.HOST_TEST_PROJECTS)[dev.ESPIDF_CPP_TEST_PROJECT],
                )
            ],
        )

    def test_test_hal_stub_runs_only_the_hal_stub_suite(self):
        self.assertEqual(dev.cmd_test_hal_stub(), 0)
        self.assertEqual(
            self.calls,
            [
                dev._idf(dev.HAL_STUB_TEST_PROJECT, dev.TEST_BUILD_DIR,
                         "--preview", "set-target", "linux"),
                dev._idf(dev.HAL_STUB_TEST_PROJECT, dev.TEST_BUILD_DIR,
                         "build"),
                [f"./{dev.HAL_STUB_TEST_PROJECT}/{dev.TEST_BUILD_DIR}/"
                 "esp32_sensors_hal_stub_tests.elf"],
            ],
        )

    def test_test_hal_common_runs_only_the_hal_common_suite(self):
        self.assertEqual(dev.cmd_test_hal_common(), 0)
        self.assertEqual(
            self.calls,
            [
                dev._idf(dev.HAL_COMMON_TEST_PROJECT, dev.TEST_BUILD_DIR,
                         "--preview", "set-target", "linux"),
                dev._idf(dev.HAL_COMMON_TEST_PROJECT, dev.TEST_BUILD_DIR,
                         "build"),
                [f"./{dev.HAL_COMMON_TEST_PROJECT}/{dev.TEST_BUILD_DIR}/"
                 "esp32_sensors_hal_common_tests.elf"],
            ],
        )

    def test_test_linux_runs_only_the_test_suite(self):
        self.assertEqual(dev.cmd_test_linux(), 0)
        self.assertEqual(
            self.calls,
            [
                dev._idf(dev.TEST_PROJECT, dev.TEST_BUILD_DIR,
                         "--preview", "set-target", "linux"),
                dev._idf(dev.TEST_PROJECT, dev.TEST_BUILD_DIR, "build"),
                [f"./{dev.TEST_PROJECT}/{dev.TEST_BUILD_DIR}/"
                 "esp32_sensors_tests.elf"],
            ],
        )

    def test_test_espidf_cpp_runs_only_the_wrapper_suite(self):
        self.assertEqual(dev.cmd_test_espidf_cpp(), 0)
        self.assertEqual(
            self.calls,
            [
                dev._idf(dev.ESPIDF_CPP_TEST_PROJECT, dev.TEST_BUILD_DIR,
                         "--preview", "set-target", "linux"),
                dev._idf(dev.ESPIDF_CPP_TEST_PROJECT, dev.TEST_BUILD_DIR,
                         "build"),
                [f"./{dev.ESPIDF_CPP_TEST_PROJECT}/{dev.TEST_BUILD_DIR}/"
                 "esp32_sensors_espidf_cpp_tests.elf"],
            ],
        )

    def test_test_runs_every_suite_in_table_order(self):
        # The regression this pins: `test` used to be an alias of
        # `test-linux`, so the espidf_cpp suite was never run by the gate.
        self.assertEqual(dev.cmd_test(), 0)
        executed = [
            Path(cmd[-1]).name
            for cmd in self.calls
            if cmd[-1].endswith(".elf")
        ]
        self.assertEqual(
            executed,
            [elf for _, elf in dev.HOST_TEST_PROJECTS],
        )
        expected = [
            cmd
            for project, elf in dev.HOST_TEST_PROJECTS
            for cmd in (
                dev._idf(project, dev.TEST_BUILD_DIR,
                         "--preview", "set-target", "linux"),
                dev._idf(project, dev.TEST_BUILD_DIR, "build"),
                [f"./{project}/{dev.TEST_BUILD_DIR}/{elf}"],
            )
        ]
        self.assertEqual(self.calls, expected)

    def test_a_failing_suite_stops_the_run_and_fails_the_command(self):
        # A red suite (here: the first ELF to run) must fail `test`, not be
        # swallowed while the later suites are run anyway.
        first_project, first_elf = dev.HOST_TEST_PROJECTS[0]
        failing = f"./{first_project}/{dev.TEST_BUILD_DIR}/{first_elf}"

        def fake_run(cmd, cwd=dev.REPO_ROOT):
            self.calls.append(list(cmd))
            return 3 if list(cmd) == [failing] else 0

        dev.run = fake_run
        self.assertEqual(dev.cmd_test(), 1)
        executed = [cmd[-1] for cmd in self.calls if cmd[-1].endswith(".elf")]
        self.assertEqual(executed, [failing])

    def test_every_host_test_project_has_a_command_that_runs_it(self):
        # `test` covers the table, but each suite is also runnable alone. This
        # pins that the per-suite commands between them cover the whole table,
        # so a suite cannot be reachable only through `test`.
        covered = {
            project
            for handler in (
                dev.cmd_test_hal_stub,
                dev.cmd_test_hal_common,
                dev.cmd_test_linux,
                dev.cmd_test_espidf_cpp,
            )
            for project, _ in dev._host_test_projects(
                self._projects_run_by(handler)
            )
        }
        self.assertEqual(covered, {p for p, _ in dev.HOST_TEST_PROJECTS})

    def _projects_run_by(self, handler):
        """The host-test projects `handler` selects (by intercepting `run`)."""
        self.calls = []
        handler()
        return tuple(
            cmd[2]
            for cmd in self.calls
            if cmd[:2] == ["idf.py", "-C"]
            and cmd[-1] == "build"
        )

    def test_unknown_project_trips_an_assertion(self):
        # A stale constant would silently run fewer suites; fail loudly.
        with self.assertRaises(AssertionError):
            dev._host_test_projects(("mcgowan",))

    def test_no_project_trips_an_assertion(self):
        with self.assertRaises(AssertionError):
            dev._run_host_tests(())


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

    These guard the bug where `install-hooks` reported success (because it
    only set `core.hooksPath`) while the hook file itself was missing, so
    commit-time checks were silently skipped.
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
        on_disk = (
            dev.REPO_ROOT / self.GITHOOKS_DIR / self.HOOK_NAME
        ).read_text()
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
    """`_unique_tidy_units` picks one entry per C++ source in the repo."""

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
        """The paths `_source_file_command` reports for a scratch repo."""
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
        self.assertTrue(
            any(p.endswith("src/real.cpp") for p in listed), listed
        )
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


class PythonSourceDiscoveryTests(unittest.TestCase):
    """`python_sources` must find Python that is not named `*.py`.

    The one script that matters most is `.githooks/pre-commit`: Python by
    shebang, with no extension, and the gate that runs before every commit. A
    file-name-only walk would silently skip it, which is why selection also
    reads the first line. Vendored trees (`third_party/`) are ours to keep
    byte-identical, so they are dropped even though the walk is
    repository-wide.
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

    # -- vendored trees -----------------------------------------------------

    def test_vendored_paths_are_recognised(self):
        # Only a top-level `third_party`; a directory merely *named* that
        # deeper in the tree is ours and must stay in scope.
        self.assertTrue(dev._is_vendored_path("third_party/json/json.hpp"))
        self.assertTrue(dev._is_vendored_path("third_party"))
        self.assertFalse(dev._is_vendored_path("components/third_party/x.py"))
        self.assertFalse(dev._is_vendored_path("scripts/dev.py"))
        self.assertFalse(dev._is_vendored_path("third_partyish/x.py"))

    # -- command ------------------------------------------------------------

    def test_command_uses_the_container_interpreter_module_form(self):
        # `python3 -m pycodestyle` (not the console script) so the checker is
        # the one belonging to the container's interpreter; defaults only, so
        # the result matches a bare `pycodestyle` run.
        self.assertEqual(
            dev._pycodestyle_command(["a.py", "b.py"]),
            ["python3", "-m", "pycodestyle", "a.py", "b.py"],
        )

    def test_command_passes_no_options_of_its_own(self):
        # An option here would make this gate and `pycodestyle` disagree; the
        # repository conforms to the tool's defaults (79 columns).
        self.assertEqual(dev.PYCODESTYLE_ARGS, ())

    # -- selection ----------------------------------------------------------

    def test_selection_keeps_dot_py_files(self):
        repo = self._scratch_repo()
        self._write(repo, "scripts/a.py", "")
        self._write(repo, "scripts/b.cpp", "")
        self.assertEqual(
            dev._select_python_sources(
                ["scripts/a.py", "scripts/b.cpp"], repo
            ),
            ["scripts/a.py"],
        )

    def test_selection_finds_a_python_shebang_without_an_extension(self):
        # The regression class: `.githooks/pre-commit` is Python with no
        # suffix, so a name-only filter drops the most important script.
        repo = self._scratch_repo()
        self._write(repo, ".githooks/pre-commit", "#!/usr/bin/env python3\n")
        self.assertEqual(
            dev._select_python_sources([".githooks/pre-commit"], repo),
            [".githooks/pre-commit"],
        )

    def test_selection_accepts_the_other_python_shebang_forms(self):
        repo = self._scratch_repo()
        for name, first_line in (
            ("bin/a", "#!/usr/bin/python\n"),
            ("bin/b", "#!/usr/bin/env python3.12\n"),
            ("bin/c", "#!/opt/esp/python_env/bin/python3\n"),
        ):
            with self.subTest(name=name):
                self._write(repo, name, first_line)
                self.assertEqual(
                    dev._select_python_sources([name], repo), [name]
                )

    def test_selection_rejects_a_non_python_shebang(self):
        # A bash script is not a Python file just because it has a shebang.
        repo = self._scratch_repo()
        self._write(repo, "bin/run.sh", "#!/bin/bash\necho hi\n")
        self.assertEqual(dev._select_python_sources(["bin/run.sh"], repo), [])

    def test_selection_rejects_a_python_mention_that_is_not_a_shebang(self):
        # Only the FIRST line counts, and it must start with `#!`.
        repo = self._scratch_repo()
        self._write(repo, "notes.txt", "run this with python3\n")
        self.assertEqual(dev._select_python_sources(["notes.txt"], repo), [])

    def test_selection_drops_vendored_trees(self):
        repo = self._scratch_repo()
        self._write(repo, "third_party/json/setup.py", "")
        self._write(repo, "scripts/ours.py", "")
        self.assertEqual(
            dev._select_python_sources(
                ["third_party/json/setup.py", "scripts/ours.py"], repo
            ),
            ["scripts/ours.py"],
        )

    def test_selection_drops_paths_that_are_not_files(self):
        # git still lists a tracked file that has been deleted; pycodestyle
        # would fail on it.
        repo = self._scratch_repo()
        self._write(repo, "kept.py", "")
        self.assertEqual(
            dev._select_python_sources(["kept.py", "gone.py"], repo),
            ["kept.py"],
        )

    def test_selection_is_sorted_and_deduplicated(self):
        repo = self._scratch_repo()
        for name in ("b.py", "a.py"):
            self._write(repo, name, "")
        self.assertEqual(
            dev._select_python_sources(["b.py", "a.py", "a.py"], repo),
            ["a.py", "b.py"],
        )

    def test_selection_survives_a_non_utf8_shebang(self):
        # The first line is read as bytes, so a file that is not valid UTF-8
        # does not raise on a candidate that is not Python at all.
        repo = self._scratch_repo()
        self._write(repo, "bin/blob", "")
        (repo / "bin" / "blob").write_bytes(b"#!\xff\xfe binary\n")
        self.assertEqual(dev._select_python_sources(["bin/blob"], repo), [])

    # -- end to end, against a scratch repository ---------------------------

    def _listed(self, repo):
        """The paths `python_sources`' enumeration reports for a repo."""
        argv = dev._source_file_command(roots=(".",))[1:]
        listed = self._git(repo, *argv).splitlines()
        return dev._select_python_sources(listed, repo)

    def test_untracked_python_is_included(self):
        # The same `--cached --others --exclude-standard` semantics as
        # `format`, so a new file needs no `git add` first.
        repo = self._scratch_repo()
        self._write(repo, "scripts/new.py", "")
        self.assertIn("scripts/new.py", self._listed(repo))

    def test_ignored_python_is_excluded(self):
        repo = self._scratch_repo()
        self._write(repo, ".gitignore", "build/\n")
        self._write(repo, "build/generated.py", "")
        self._write(repo, "scripts/real.py", "")
        listed = self._listed(repo)
        self.assertIn("scripts/real.py", listed)
        self.assertNotIn("build/generated.py", listed)

    # -- the real repository ------------------------------------------------

    def test_the_repository_enumerates_its_expected_python_sources(self):
        # Pins the actual set: the two dev.py scripts and the extensionless
        # hook. A new Python source is meant to be noticed here, so this list
        # is a deliberate tripwire rather than an over-broad assertion.
        self.assertEqual(
            dev.python_sources(),
            [
                ".githooks/pre-commit",
                "scripts/dev.py",
                "scripts/test_dev.py",
            ],
        )

    def test_no_vendored_python_is_enumerated(self):
        # `third_party/sml` and `third_party/json` must never reach the gate.
        for path in dev.python_sources():
            with self.subTest(path=path):
                self.assertFalse(dev._is_vendored_path(path), path)


class PycodestyleGateTests(unittest.TestCase):
    """`pycodestyle` is wired into the pre-commit hook and into CI."""

    def test_command_is_registered_and_documented(self):
        self.assertIn("pycodestyle", dev.COMMANDS)
        self.assertIs(dev.COMMANDS["pycodestyle"], dev.cmd_pycodestyle)
        self.assertTrue((dev.cmd_pycodestyle.__doc__ or "").strip())

    def test_command_runs_in_the_container(self):
        # pycodestyle is installed in the dev image, not on the host.
        source = (dev.REPO_ROOT / "scripts" / "dev.py").read_text()
        body = source.split("def cmd_pycodestyle", 1)[1].split("\ndef ", 1)[0]
        self.assertIn("change_to_container()", body)

    def test_hook_runs_pycodestyle_first(self):
        # The cheapest gate: it needs no compile database, no build, and no
        # test run, so it must come before the others rather than after them.
        hook = (dev.REPO_ROOT / ".githooks" / "pre-commit").read_text()
        style = hook.index('run_check(dev, "pycodestyle")')
        for later in (
            '"clang-tidy"',
            '"format-check-staged"',
            '"test"',
        ):
            with self.subTest(later=later):
                self.assertLess(style, hook.index(f"run_check(dev, {later})"))

    def test_ci_runs_pycodestyle_before_the_build(self):
        # It needs no compile database, so there is no reason to wait for the
        # toolchain: a long line is reported in seconds.
        workflow = (
            dev.REPO_ROOT / ".github" / "workflows" / "ci.yml"
        ).read_text()
        style = workflow.index("python3 ./scripts/dev.py pycodestyle")
        self.assertLess(
            style, workflow.index("python3 ./scripts/dev.py build")
        )

    def test_the_image_installs_pycodestyle(self):
        # The gate would otherwise fail with "No module named pycodestyle".
        dockerfile = (dev.REPO_ROOT / "Dockerfile").read_text()
        self.assertIn("pycodestyle==", dockerfile)
        # Pinned, and verified at build time so a bad install fails the build.
        self.assertIn("ARG PYCODESTYLE_VERSION=", dockerfile)
        self.assertIn("pycodestyle --version", dockerfile)


class SelfCheckModuleTests(unittest.TestCase):
    """Sanity check on the module dev.py invokes for `self-check`.

    Also pins `self-check` itself: it is host-only, because inside the
    container none of the decisions this suite pins ever happen (no command
    re-execs itself), so a run there would pass vacuously.
    """

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

    # -- host-only ----------------------------------------------------------

    def test_refuses_to_run_inside_the_container(self):
        # Inside the container none of the decisions the suite pins ever
        # happen (a command never re-execs itself), so a run there would pass
        # vacuously. Fail loudly with a message that says what to run instead.
        calls = []
        original_run, original_in_container = dev.run, dev.is_in_container
        err = io.StringIO()
        try:
            dev.run = lambda cmd, cwd=dev.REPO_ROOT: calls.append(cmd) or 0
            dev.is_in_container = lambda: True
            with redirect_stderr(err):
                rc = dev.cmd_self_check()
        finally:
            dev.run, dev.is_in_container = original_run, original_in_container
        self.assertEqual(rc, 1)
        self.assertEqual(calls, [], "the test suite must not be run")
        message = err.getvalue()
        self.assertIn("refusing to run inside the dev container", message)
        # The message must name the way out: run it on the host, or use the
        # container's own test command instead.
        self.assertIn("./scripts/dev.py self-check", message)
        self.assertIn("./scripts/dev.py test", message)

    def test_runs_the_suite_on_the_host(self):
        calls = []
        original_run, original_in_container = dev.run, dev.is_in_container
        try:
            dev.run = lambda cmd, cwd=dev.REPO_ROOT: calls.append(cmd) or 0
            dev.is_in_container = lambda: False
            self.assertEqual(dev.cmd_self_check(), 0)
        finally:
            dev.run, dev.is_in_container = original_run, original_in_container
        self.assertEqual(
            calls,
            [["python3", "-m", "unittest", "-v", dev.SELF_CHECK_MODULE]],
        )

    def test_self_check_never_hops_into_the_container(self):
        # It is host-only by design, so it must not call
        # `change_to_container()` (which would re-exec the suite there).
        source = (dev.REPO_ROOT / "scripts" / "dev.py").read_text()
        body = source.split("def cmd_self_check", 1)[1].split("\ndef ", 1)[0]
        self.assertNotIn("change_to_container()", body)
        self.assertIn("is_in_container()", body)


if __name__ == "__main__":
    unittest.main()
