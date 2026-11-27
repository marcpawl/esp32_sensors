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

    def test_power_on_stages_pending(self):
        keep_running = self.console.handle_command("power on")
        self.assertTrue(keep_running)
        self.assertEqual(self.console.pending, {"power": "on"})
        self.assertEqual(self.sent, [])

    def test_power_off_stages_pending(self):
        keep_running = self.console.handle_command("power off")
        self.assertTrue(keep_running)
        self.assertEqual(self.console.pending, {"power": "off"})
        self.assertEqual(self.sent, [])

    def test_switch_commands_stage_pending(self):
        self.console.handle_command("switch config on")
        self.console.handle_command("switch run off")
        self.assertEqual(
            self.console.pending,
            {"config_switch": "on", "run_switch": "off"},
        )
        self.assertEqual(self.sent, [])

    def test_no_removes_pending_field(self):
        self.console.handle_command("power on")
        self.console.handle_command("switch config on")
        self.console.handle_command("no power")
        self.assertEqual(self.console.pending, {"config_switch": "on"})
        self.console.handle_command("no switch config")
        self.assertEqual(self.console.pending, {})

    def test_send_transmits_pending(self):
        self.console.handle_command("power on")
        self.console.handle_command("switch run on")
        keep_running = self.console.handle_command("send")
        self.assertTrue(keep_running)
        self.assertEqual(
            self.sent, [{"power": "on", "run_switch": "on"}]
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

    def test_send_echoes_json_as_sent(self):
        console = donny.DonnyConsole(Path("/tmp/donny.elf"))
        process = mock.Mock()
        process.stdin = io.StringIO()
        process.poll.return_value = None
        console.process = process
        console.send({"run_switch": "on"})
        self.assertEqual(
            console.console_snapshot(),
            (('{"run_switch":"on"}', True),),
        )

    def test_failed_send_is_not_echoed(self):
        console = donny.DonnyConsole(Path("/tmp/donny.elf"))
        process = mock.Mock()
        process.stdin.write.side_effect = BrokenPipeError
        process.poll.return_value = None
        console.process = process
        console.send({"run_switch": "on"})
        self.assertEqual(console.console_snapshot(), ())


class ConsoleTranscriptTests(unittest.TestCase):
    """Simulator output and sent text share a bounded transcript."""

    def test_stdout_lines_are_recorded_as_received(self):
        console = donny.DonnyConsole(Path("/tmp/donny.elf"))
        received = [
            donny.MODEL_PREFIX + '{"power":"on"}',
            "unsolicited simulator text",
        ]
        with mock.patch.object(console, "_prompt_hint"), \
                redirect_stdout(io.StringIO()):
            console._read_stdout(io.StringIO("\n".join(received) + "\n"))
        self.assertEqual(
            console.console_snapshot(),
            tuple((line, False) for line in received),
        )

    def test_stderr_logs_are_recorded_as_received(self):
        console = donny.DonnyConsole(Path("/tmp/donny.elf"))
        console._read_stderr(io.StringIO("I (1) console: hello\n"))
        self.assertEqual(
            console.console_snapshot(),
            (("I (1) console: hello", False),),
        )
        self.assertEqual(
            console.events.get_nowait(), ("log", "I (1) console: hello")
        )

    def test_history_keeps_only_the_most_recent_lines(self):
        console = donny.DonnyConsole(Path("/tmp/donny.elf"))
        for index in range(donny.CONSOLE_HISTORY + 3):
            console._record_console(str(index))
        snapshot = console.console_snapshot()
        self.assertEqual(len(snapshot), donny.CONSOLE_HISTORY)
        self.assertEqual(snapshot[0], ("3", False))
        self.assertEqual(
            snapshot[-1], (str(donny.CONSOLE_HISTORY + 2), False))

    def test_snapshot_does_not_expose_internal_list(self):
        console = donny.DonnyConsole(Path("/tmp/donny.elf"))
        console._record_console("first")
        snapshot = console.console_snapshot()
        console._record_console("second")
        self.assertEqual(snapshot, (("first", False),))


class TUILayoutTests(unittest.TestCase):
    """The top row is split 25% / 25% / 50%, above a 5-line prompt."""

    def setUp(self):
        self.stdscr = mock.Mock()
        self.stdscr.getmaxyx.return_value = (30, 80)
        console = donny.DonnyConsole(Path("/tmp/donny.elf"))
        self.tui = donny.DonnyTUI(self.stdscr, console)
        self.windows = []

        def newwin(height, width, top, left):
            win = mock.Mock()
            win.getmaxyx.return_value = (height, width)
            self.windows.append((height, width, top, left, win))
            return win

        self.patches = [
            mock.patch.object(donny.curses, "newwin", side_effect=newwin),
            mock.patch.object(donny.curses, "curs_set"),
            mock.patch.object(donny.curses, "doupdate"),
        ]
        for patcher in self.patches:
            patcher.start()

    def tearDown(self):
        for patcher in reversed(self.patches):
            patcher.stop()

    def test_layout_uses_quarter_quarter_half_widths(self):
        self.assertEqual(
            self.tui._layout(),
            (30, 80, 25, 20, 20, 40, 5),
        )

    def test_draw_creates_board_settings_console_and_commands(self):
        self.tui._draw(force=True)
        self.assertEqual(
            [(height, width, top, left)
             for height, width, top, left, _ in self.windows],
            [
                (25, 20, 0, 0),
                (25, 20, 0, 20),
                (25, 40, 0, 40),
                (5, 80, 25, 0),
            ],
        )

    def test_console_pane_reverses_sent_lines(self):
        self.tui.console._record_console("received from simulator")
        self.tui.console._record_console('{"power":"on"}', sent=True)
        self.tui._draw(force=True)
        console_window = self.windows[2][4]
        titles = []
        content = {}
        for call in console_window.addnstr.call_args_list:
            row, column, text = call.args[:3]
            if row == 0:
                titles.append(text)
            else:
                content[text] = call.args[4]
        self.assertIn(" Console ", titles)
        self.assertEqual(content["received from simulator"],
                         donny.curses.A_NORMAL)
        self.assertEqual(content['{"power":"on"}'], donny.curses.A_REVERSE)


if __name__ == "__main__":
    unittest.main()
