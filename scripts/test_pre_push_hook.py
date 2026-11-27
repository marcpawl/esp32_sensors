#!/usr/bin/env python3
"""Host-side unit tests for scripts/pre_push_hook.py."""

from __future__ import annotations

import io
import json
import subprocess
import sys
import unittest
from pathlib import Path

SCRIPTS_DIR = Path(__file__).resolve().parent
if str(SCRIPTS_DIR) not in sys.path:
    sys.path.insert(0, str(SCRIPTS_DIR))

import pre_push_hook  # noqa: E402


def _completed(stdout="", stderr="", returncode=0):
    return subprocess.CompletedProcess(
        args=["gh"], returncode=returncode, stdout=stdout, stderr=stderr
    )


class ParseGitHubRepoTests(unittest.TestCase):
    def test_accepts_scp_like_ssh(self):
        self.assertEqual(
            pre_push_hook.parse_github_repo(
                "git@github.com:marcpawl/esp32_sensors.git"
            ),
            ("marcpawl", "esp32_sensors"),
        )

    def test_accepts_https(self):
        self.assertEqual(
            pre_push_hook.parse_github_repo(
                "https://github.com/marcpawl/esp32_sensors.git"
            ),
            ("marcpawl", "esp32_sensors"),
        )

    def test_accepts_http(self):
        self.assertEqual(
            pre_push_hook.parse_github_repo(
                "http://github.com/marcpawl/esp32_sensors"
            ),
            ("marcpawl", "esp32_sensors"),
        )

    def test_accepts_ssh_url_with_user(self):
        self.assertEqual(
            pre_push_hook.parse_github_repo(
                "ssh://git@github.com/marcpawl/esp32_sensors.git"
            ),
            ("marcpawl", "esp32_sensors"),
        )

    def test_accepts_ssh_url_without_user(self):
        self.assertEqual(
            pre_push_hook.parse_github_repo(
                "ssh://github.com/marcpawl/esp32_sensors.git"
            ),
            ("marcpawl", "esp32_sensors"),
        )

    def test_accepts_case_insensitive_host_and_scheme(self):
        self.assertEqual(
            pre_push_hook.parse_github_repo(
                "HTTPS://GitHub.COM/marcpawl/esp32_sensors.git"
            ),
            ("marcpawl", "esp32_sensors"),
        )

    def test_rejects_non_github_host(self):
        self.assertIsNone(
            pre_push_hook.parse_github_repo(
                "git@gitlab.com:marcpawl/esp32_sensors.git"
            )
        )

    def test_rejects_lookalike_host(self):
        self.assertIsNone(
            pre_push_hook.parse_github_repo(
                "https://github.com.evil.com/marcpawl/esp32_sensors.git"
            )
        )
        self.assertIsNone(
            pre_push_hook.parse_github_repo(
                "https://notgithub.com/marcpawl/esp32_sensors.git"
            )
        )

    def test_rejects_non_github_url_forms(self):
        self.assertIsNone(
            pre_push_hook.parse_github_repo(
                "file:///home/user/repo.git"
            )
        )
        self.assertIsNone(pre_push_hook.parse_github_repo(""))

    def test_github_prefix_with_bad_owner_repo_fails_closed(self):
        with self.assertRaises(pre_push_hook.HookError):
            pre_push_hook.parse_github_repo("git@github.com:broken")
        with self.assertRaises(pre_push_hook.HookError):
            pre_push_hook.parse_github_repo("https://github.com/owner-only")


class IterPushedBranchesTests(unittest.TestCase):
    def test_extracts_branch_updates(self):
        lines = [
            "refs/heads/a 111 refs/heads/a 222\n",
            "refs/heads/b 333 refs/heads/b 444\n",
        ]
        self.assertEqual(
            pre_push_hook.iter_pushed_branches(lines), ["a", "b"]
        )

    def test_ignores_deletions_tags_and_duplicates(self):
        zero = "0" * 40
        lines = [
            f"{zero} {zero} refs/heads/gone {'1' * 40}\n",
            f"{'1' * 40} {'2' * 40} refs/tags/v1 {'0' * 40}\n",
            f"{'1' * 40} {'2' * 40} refs/heads/a {'3' * 40}\n",
            f"{'4' * 40} {'5' * 40} refs/heads/a {'6' * 40}\n",
        ]
        self.assertEqual(pre_push_hook.iter_pushed_branches(lines), ["a"])

    def test_malformed_line_fails_closed(self):
        with self.assertRaises(pre_push_hook.HookError):
            pre_push_hook.iter_pushed_branches(["bad line\n"])


class GitHubInteractionTests(unittest.TestCase):
    def test_list_open_prs_parses_json(self):
        calls = []

        def runner(args, **kwargs):
            calls.append(args)
            return _completed(
                stdout=json.dumps([
                    {"number": 12, "autoMergeRequest": None},
                    {"number": 13, "autoMergeRequest": {"enabledAt": "x"}},
                ])
            )

        prs = pre_push_hook.list_open_prs(
            "marcpawl/esp32_sensors", "feature", runner=runner
        )
        self.assertEqual(len(prs), 2)
        self.assertEqual(
            calls[0],
            [
                "gh", "pr", "list",
                "-R", "marcpawl/esp32_sensors",
                "--head", "feature",
                "--state", "open",
                "--json", "number,autoMergeRequest",
            ],
        )

    def test_list_open_prs_bad_json_fails_closed(self):
        with self.assertRaises(pre_push_hook.HookError):
            pre_push_hook.list_open_prs(
                "o/r", "b", runner=lambda *a, **k: _completed(stdout="{")
            )

    def test_disable_auto_merge_runs_expected_command(self):
        calls = []

        def runner(args, **kwargs):
            calls.append(args)
            return _completed()

        pre_push_hook.disable_auto_merge("o/r", 42, runner=runner)
        self.assertEqual(
            calls[0],
            ["gh", "pr", "merge", "42", "-R", "o/r", "--disable-auto"],
        )

    def test_command_failure_fails_closed(self):
        with self.assertRaises(pre_push_hook.HookError) as ctx:
            pre_push_hook.disable_auto_merge(
                "o/r",
                7,
                runner=lambda *a, **k: _completed(
                    returncode=1, stderr="bad auth"
                ),
            )
        self.assertIn("bad auth", str(ctx.exception))

    def test_missing_gh_fails_closed(self):
        def runner(*args, **kwargs):
            raise FileNotFoundError("gh")

        with self.assertRaises(pre_push_hook.HookError) as ctx:
            pre_push_hook.list_open_prs("o/r", "b", runner=runner)
        self.assertIn("command not found", str(ctx.exception))


class RunHookTests(unittest.TestCase):
    def test_non_github_remote_is_noop(self):
        calls = []
        err = io.StringIO()
        rc = pre_push_hook.run_hook(
            "origin",
            "git@gitlab.com:o/r.git",
            io.StringIO("refs/heads/a 1 2 refs/heads/a 3\n"),
            runner=lambda *a, **k: calls.append(a) or _completed(),
            err=err,
        )
        self.assertEqual(rc, 0)
        self.assertEqual(calls, [])

    def test_disables_auto_merge_for_enabled_pr(self):
        calls = []

        def runner(args, **kwargs):
            calls.append(args)
            if args[:3] == ["gh", "pr", "list"]:
                return _completed(
                    stdout=json.dumps([
                        {"number": 5, "autoMergeRequest": {"enabledAt": "x"}},
                        {"number": 6, "autoMergeRequest": None},
                    ])
                )
            return _completed()

        err = io.StringIO()
        rc = pre_push_hook.run_hook(
            "origin",
            "git@github.com:o/r.git",
            io.StringIO(
                "1" * 40 + " " + "2" * 40 + " refs/heads/feature " + "3" * 40
                + "\n"
            ),
            runner=runner,
            err=err,
        )
        self.assertEqual(rc, 0)
        self.assertEqual(len(calls), 2)
        self.assertEqual(
            calls[1],
            ["gh", "pr", "merge", "5", "-R", "o/r", "--disable-auto"],
        )
        self.assertIn("disabled auto-merge", err.getvalue())

    def test_github_failure_blocks_push(self):
        err = io.StringIO()
        rc = pre_push_hook.run_hook(
            "origin",
            "git@github.com:o/r.git",
            io.StringIO(
                "1" * 40 + " " + "2" * 40 + " refs/heads/feature " + "3" * 40
                + "\n"
            ),
            runner=lambda *a, **k: _completed(returncode=1, stderr="no auth"),
            err=err,
        )
        self.assertEqual(rc, 1)
        self.assertIn("push blocked", err.getvalue())

    def test_main_requires_remote_args(self):
        err = io.StringIO()
        rc = pre_push_hook.main([], stdin=io.StringIO(""), err=err)
        self.assertEqual(rc, 1)
        self.assertIn("expected remote name", err.getvalue())


if __name__ == "__main__":
    unittest.main()
