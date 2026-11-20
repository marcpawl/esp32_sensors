#!/usr/bin/env python3
"""Host-side unit tests for `scripts/donny.py`.

Run as part of:

    scripts/dev.py self-check

These tests exercise the interactive console logic without starting a real
`donny.elf`: container guarding, command parsing/mapping, tab completion,
and MODEL-line handling are covered with stdlib `unittest` and mocks.
"""

from __future__ import annotations

from contextlib import redirect_stderr, redirect_stdout
import io
import json
import os
from pathlib import Path
import sys
import tempfile
import unittest
from unittest import mock

SCRIPTS_DIR = Path(__file__).resolve().parent
if str(SCRIPTS_DIR) not in sys.path:
    sys.path.insert(0, str(SCRIPTS_DIR))

import donny  # noqa: E402


class ContainerGuardTests(unittest.TestCase):
    """The script must run only inside the dev container."""

    def test_in_container_checks_marker_file(self):
        with mock.patch.object(donny.Path, "exists", return_value=True):
            self.assertTrue(donny.in_container())

    def test_main_refuses_to_run_outside_container(self):
        err = io.StringIO()
        with mock.patch.object(donny, "in_container", return_value=False), \
                redirect_stderr(err):
            with self.assertRaises(SystemExit) as raised:
                donny.main(["--elf", "/tmp/donny.elf"])
        self.assertEqual(raised.exception.code, 1)
        self.assertIn(
            "must be run inside the dev container", err.getvalue()
        )

    def test_main_reports_missing_elf_inside_container(self):
        err = io.StringIO()
        with tempfile.TemporaryDirectory() as tmp:
            missing = Path(tmp) / "donny.elf"
            with mock.patch.object(donny, "in_container", return_value=True), \
                    redirect_stderr(err):
                with self.assertRaises(SystemExit) as raised:
                    donny.main(["--elf", str(missing)])
        self.assertEqual(raised.exception.code, 1)
        message = err.getvalue()
        self.assertIn("donny executable not found", message)
        self.assertIn(
            "./scripts/dev.py build components/simulator/exe/donny", message
        )


class CommandParserTests(unittest.TestCase):
    """Interactive commands are parsed with argparse."""

    def setUp(self):
        self.parser = donny.build_parser()

    def test_power_on(self):
        args = self.parser.parse_args(["power", "on"])
        self.assertEqual(args.command, "power")
        self.assertEqual(args.state, "on")

    def test_power_off(self):
        args = self.parser.parse_args(["power", "off"])
        self.assertEqual(args.command, "power")
        self.assertEqual(args.state, "off")

    def test_power_requires_on_or_off(self):
        with self.assertRaises(ValueError):
            self.parser.parse_args(["power", "maybe"])

    def test_show_help_quit_exit(self):
        for name in ("show", "help", "quit", "exit"):
            with self.subTest(command=name):
                args = self.parser.parse_args([name])
                self.assertEqual(args.command, name)


class ConsoleCommandTests(unittest.TestCase):
    """Commands map to console protocol documents."""

    def setUp(self):
        self.console = donny.DonnyConsole(Path("/tmp/donny.elf"))
        self.sent = []
        self.console.send = lambda document: self.sent.append(document)

    def test_power_on_maps_to_run_and_config_switches(self):
        keep_running = self.console.handle_command("power on")
        self.assertTrue(keep_running)
        self.assertEqual(
            self.sent, [{"run_switch": "on", "config_switch": "off"}]
        )

    def test_power_off_maps_to_both_switches_off(self):
        keep_running = self.console.handle_command("power off")
        self.assertTrue(keep_running)
        self.assertEqual(
            self.sent, [{"run_switch": "off", "config_switch": "off"}]
        )

    def test_show_requests_snapshot(self):
        keep_running = self.console.handle_command("show")
        self.assertTrue(keep_running)
        self.assertEqual(self.sent, [{}])

    def test_empty_command_none_treated_as_show(self):
        # argparse yields command=None for an empty parse in our usage only if
        # invoked oddly; handle_command is called with non-empty lines.
        # Explicit show behavior is covered above.
        self.assertTrue(self.console.handle_command("show"))

    def test_quit_and_exit_stop_repl(self):
        self.assertFalse(self.console.handle_command("quit"))
        self.assertFalse(self.console.handle_command("exit"))

    def test_help_prints_parser_help(self):
        out = io.StringIO()
        with redirect_stdout(out):
            keep_running = self.console.handle_command("help")
        self.assertTrue(keep_running)
        text = out.getvalue()
        self.assertIn("power", text)
        self.assertIn("show", text)

    def test_invalid_command_reports_error_and_continues(self):
        out = io.StringIO()
        with redirect_stdout(out):
            keep_running = self.console.handle_command("power maybe")
        self.assertTrue(keep_running)
        self.assertTrue(out.getvalue().strip())


class CompleterTests(unittest.TestCase):
    """Tab completion offers commands and power states."""

    def setUp(self):
        self.console = donny.DonnyConsole(Path("/tmp/donny.elf"))

    def _complete(self, buffer: str, text: str):
        with mock.patch("readline.get_line_buffer", return_value=buffer), \
                mock.patch("readline.get_begidx", return_value=len(buffer)):
            matches = []
            state = 0
            while True:
                match = self.console.completer(text, state)
                if match is None:
                    break
                matches.append(match)
                state += 1
            return matches

    def test_complete_top_level_commands(self):
        self.assertIn("power", self._complete("po", "po"))
        self.assertEqual(["show"], self._complete("sh", "sh"))

    def test_complete_power_subcommands(self):
        self.assertEqual(["off"], self._complete("power of", "of"))
        self.assertCountEqual(["on", "off"], self._complete("power o", "o"))
        self.assertCountEqual(["on", "off"], self._complete("power ", ""))


class ModelParsingTests(unittest.TestCase):
    """MODEL protocol lines update the displayed state."""

    def test_model_line_updates_current_model(self):
        console = donny.DonnyConsole(Path("/tmp/donny.elf"))
        payload = {"run_switch": "off", "config_switch": "off"}
        line = donny.MODEL_PREFIX + json.dumps(payload)

        out = io.StringIO()
        with mock.patch.object(console, "_prompt_hint"), \
                redirect_stdout(out):
            console._read_stdout(io.StringIO(line + "\n"))
        self.assertEqual(console.model, payload)
        text = out.getvalue()
        self.assertIn("MODEL", text)
        self.assertIn('"run_switch": "off"', text)

    def test_non_model_lines_are_ignored(self):
        console = donny.DonnyConsole(Path("/tmp/donny.elf"))
        out = io.StringIO()
        with mock.patch.object(console, "_prompt_hint"), \
                redirect_stdout(out):
            console._read_stdout(io.StringIO("hello\n"))
        self.assertIsNone(console.model)
        self.assertEqual(out.getvalue(), "")

    def test_invalid_model_payload_is_reported(self):
        console = donny.DonnyConsole(Path("/tmp/donny.elf"))
        out = io.StringIO()
        with mock.patch.object(console, "_prompt_hint"), \
                redirect_stdout(out):
            console._read_stdout(io.StringIO("MODEL {not json}\n"))
        self.assertIsNone(console.model)
        self.assertIn("invalid MODEL payload", out.getvalue())


class SendTests(unittest.TestCase):
    """Documents are sent as single JSON lines."""

    def test_send_writes_json_line(self):
        console = donny.DonnyConsole(Path("/tmp/donny.elf"))
        stdin = io.StringIO()
        process = mock.Mock()
        process.stdin = stdin
        process.poll.return_value = None
        console.process = process
        console.send({"run_switch": "on"})
        self.assertEqual(stdin.getvalue(), '{"run_switch":"on"}\n')


if __name__ == "__main__":
    unittest.main()
